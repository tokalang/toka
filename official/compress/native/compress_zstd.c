#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <zstd.h>

enum {
  TOKA_ZSTD_ENCODER = 0,
  TOKA_ZSTD_DECODER = 1,
};

enum {
  TOKA_ZSTD_OK = 0,
  TOKA_ZSTD_INVALID = -1,
  TOKA_ZSTD_CLOSED = -2,
  TOKA_ZSTD_OOM = -3,
  TOKA_ZSTD_ERROR = -4,
  TOKA_ZSTD_TRUNCATED = -5,
  TOKA_ZSTD_TRAILING = -6,
  TOKA_ZSTD_LIMIT = -7,
};

typedef struct {
  ZSTD_CStream *cstream;
  ZSTD_DStream *dstream;
  int direction;
  int initialized;
  int finished;
  size_t output_limit;
  size_t total_output;
  unsigned char *output;
  size_t output_len;
  size_t output_cap;
} TokaZstd;

static int toka_zstd_live_handles = 0;

static void clear_output(TokaZstd *state) {
  free(state->output);
  state->output = NULL;
  state->output_len = 0;
  state->output_cap = 0;
}

static void end_stream(TokaZstd *state) {
  if (!state->initialized)
    return;
  if (state->direction == TOKA_ZSTD_ENCODER) {
    if (state->cstream != NULL) {
      ZSTD_freeCStream(state->cstream);
      state->cstream = NULL;
    }
  } else {
    if (state->dstream != NULL) {
      ZSTD_freeDStream(state->dstream);
      state->dstream = NULL;
    }
  }
  state->initialized = 0;
}

static int ensure_output(TokaZstd *state, size_t needed) {
  size_t next_cap = state->output_cap;
  size_t max_cap = SIZE_MAX;
  if (state->direction == TOKA_ZSTD_DECODER && state->output_limit != 0) {
    if (state->total_output > state->output_limit)
      return TOKA_ZSTD_LIMIT;
    max_cap = state->output_limit - state->total_output;
    if (max_cap != SIZE_MAX)
      max_cap += 1;
  }
  if (needed > max_cap)
    needed = max_cap;
  if (needed <= state->output_cap)
    return TOKA_ZSTD_OK;
  if (next_cap == 0)
    next_cap = 256;
  while (next_cap < needed) {
    if (next_cap > max_cap / 2) {
      next_cap = max_cap;
      break;
    }
    next_cap *= 2;
  }
  if (next_cap < needed || next_cap == 0)
    return TOKA_ZSTD_LIMIT;
  unsigned char *next = realloc(state->output, next_cap);
  if (next == NULL)
    return TOKA_ZSTD_OOM;
  state->output = next;
  state->output_cap = next_cap;
  return TOKA_ZSTD_OK;
}

uintptr_t toka_zstd_new(int direction, int level, size_t output_limit) {
  if ((direction != TOKA_ZSTD_ENCODER && direction != TOKA_ZSTD_DECODER) ||
      (direction == TOKA_ZSTD_ENCODER && (level < -1 || level > 22)) ||
      (direction == TOKA_ZSTD_DECODER && output_limit == 0))
    return 0;

  TokaZstd *state = calloc(1, sizeof(*state));
  if (state == NULL)
    return 0;
  state->direction = direction;
  state->output_limit = output_limit;

  if (direction == TOKA_ZSTD_ENCODER) {
    state->cstream = ZSTD_createCStream();
    if (state->cstream == NULL) {
      free(state);
      return 0;
    }
    int eff_level = (level == -1) ? 3 : level;
    if (ZSTD_isError(ZSTD_CCtx_setParameter(state->cstream, ZSTD_c_compressionLevel, eff_level)) ||
        ZSTD_isError(ZSTD_CCtx_setParameter(state->cstream, ZSTD_c_checksumFlag, 1))) {
      ZSTD_freeCStream(state->cstream);
      free(state);
      return 0;
    }
  } else {
    state->dstream = ZSTD_createDStream();
    if (state->dstream == NULL) {
      free(state);
      return 0;
    }
    // Set maximum decompression window log (27 = 128 MB max window) to prevent memory exhaustion
    if (ZSTD_isError(ZSTD_DCtx_setParameter(state->dstream, ZSTD_d_windowLogMax, 27))) {
      ZSTD_freeDStream(state->dstream);
      free(state);
      return 0;
    }
  }

  state->initialized = 1;
  toka_zstd_live_handles++;
  return (uintptr_t)state;
}

static int prepare_output(TokaZstd *state, ZSTD_outBuffer *out_buf) {
  if (state->output_len == state->output_cap) {
    if (state->output_cap == SIZE_MAX)
      return TOKA_ZSTD_LIMIT;
    int status = ensure_output(state, state->output_cap + 1);
    if (status != TOKA_ZSTD_OK)
      return status;
  }
  out_buf->dst = state->output;
  out_buf->size = state->output_cap;
  out_buf->pos = state->output_len;
  return TOKA_ZSTD_OK;
}

static int note_output(TokaZstd *state, size_t pos_after) {
  state->output_len = pos_after;
  if (state->direction == TOKA_ZSTD_DECODER && state->output_limit != 0 &&
      state->output_len > state->output_limit - state->total_output)
    return TOKA_ZSTD_LIMIT;
  return TOKA_ZSTD_OK;
}

static int process_encoder(TokaZstd *state, const unsigned char *input,
                           size_t input_len, int finish) {
  ZSTD_inBuffer in_buf = { .src = input, .size = input_len, .pos = 0 };
  ZSTD_EndDirective mode = finish ? ZSTD_e_end : ZSTD_e_continue;

  while (1) {
    ZSTD_outBuffer out_buf;
    int prepared = prepare_output(state, &out_buf);
    if (prepared != TOKA_ZSTD_OK)
      return prepared;

    size_t res = ZSTD_compressStream2(state->cstream, &out_buf, &in_buf, mode);
    int noted = note_output(state, out_buf.pos);
    if (noted != TOKA_ZSTD_OK)
      return noted;

    if (ZSTD_isError(res))
      return TOKA_ZSTD_ERROR;

    if (finish) {
      if (res == 0) {
        state->finished = 1;
        end_stream(state);
        return TOKA_ZSTD_OK;
      }
    } else {
      if (in_buf.pos == in_buf.size)
        return TOKA_ZSTD_OK;
    }
  }
}

static int process_decoder(TokaZstd *state, const unsigned char *input,
                           size_t input_len, int finish) {
  ZSTD_inBuffer in_buf = { .src = input, .size = input_len, .pos = 0 };

  while (1) {
    ZSTD_outBuffer out_buf;
    int prepared = prepare_output(state, &out_buf);
    if (prepared != TOKA_ZSTD_OK)
      return prepared;

    size_t res = ZSTD_decompressStream(state->dstream, &out_buf, &in_buf);
    int noted = note_output(state, out_buf.pos);
    if (noted != TOKA_ZSTD_OK)
      return noted;

    if (ZSTD_isError(res)) {
      return TOKA_ZSTD_ERROR;
    }

    if (res == 0) {
      // Completed frame reached
      if (in_buf.pos < in_buf.size)
        return TOKA_ZSTD_TRAILING;
      state->finished = 1;
      end_stream(state);
      return TOKA_ZSTD_OK;
    }

    if (finish && in_buf.pos == in_buf.size) {
      return TOKA_ZSTD_TRUNCATED;
    }

    if (!finish && in_buf.pos == in_buf.size) {
      return TOKA_ZSTD_OK;
    }
  }
}

int toka_zstd_process(uintptr_t raw, uintptr_t input_raw, size_t input_len,
                      int finish) {
  TokaZstd *state = (TokaZstd *)raw;
  if (state == NULL)
    return TOKA_ZSTD_INVALID;
  if (state->finished)
    return finish && input_len == 0 ? TOKA_ZSTD_OK : TOKA_ZSTD_CLOSED;
  if (!state->initialized)
    return TOKA_ZSTD_INVALID;
  if (input_len != 0 && input_raw == 0)
    return TOKA_ZSTD_INVALID;

  clear_output(state);
  int status;
  if (state->direction == TOKA_ZSTD_ENCODER)
    status = process_encoder(state, (const unsigned char *)input_raw, input_len, finish);
  else
    status = process_decoder(state, (const unsigned char *)input_raw, input_len, finish);
  if (status == TOKA_ZSTD_OK) {
    state->total_output += state->output_len;
    return TOKA_ZSTD_OK;
  }
  clear_output(state);
  return status;
}

size_t toka_zstd_output_len(uintptr_t raw) {
  TokaZstd *state = (TokaZstd *)raw;
  return state == NULL ? 0 : state->output_len;
}

size_t toka_zstd_output_cap(uintptr_t raw) {
  TokaZstd *state = (TokaZstd *)raw;
  return state == NULL ? 0 : state->output_cap;
}

uintptr_t toka_zstd_take_output(uintptr_t raw) {
  TokaZstd *state = (TokaZstd *)raw;
  if (state == NULL)
    return 0;
  uintptr_t output = (uintptr_t)state->output;
  state->output = NULL;
  state->output_len = 0;
  state->output_cap = 0;
  return output;
}

void toka_zstd_free(uintptr_t raw) {
  TokaZstd *state = (TokaZstd *)raw;
  if (state == NULL)
    return;
  clear_output(state);
  end_stream(state);
  free(state);
  toka_zstd_live_handles--;
}

int toka_zstd_live_handle_count(void) { return toka_zstd_live_handles; }
