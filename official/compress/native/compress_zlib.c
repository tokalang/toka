#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <zlib.h>

enum {
  TOKA_COMPRESS_ENCODER = 0,
  TOKA_COMPRESS_DECODER = 1,
  TOKA_COMPRESS_GZIP = 0,
  TOKA_COMPRESS_ZLIB = 1,
};

enum {
  TOKA_COMPRESS_OK = 0,
  TOKA_COMPRESS_INVALID = -1,
  TOKA_COMPRESS_CLOSED = -2,
  TOKA_COMPRESS_OOM = -3,
  TOKA_COMPRESS_ZLIB_ERROR = -4,
  TOKA_COMPRESS_TRUNCATED = -5,
  TOKA_COMPRESS_TRAILING = -6,
  TOKA_COMPRESS_LIMIT = -7,
};

typedef struct {
  z_stream stream;
  int direction;
  int initialized;
  int finished;
  size_t output_limit;
  size_t total_output;
  unsigned char *output;
  size_t output_len;
  size_t output_cap;
} TokaCompress;

static int toka_compress_live_handles = 0;

static void clear_output(TokaCompress *state) {
  free(state->output);
  state->output = NULL;
  state->output_len = 0;
  state->output_cap = 0;
}

static void end_stream(TokaCompress *state) {
  if (!state->initialized)
    return;
  if (state->direction == TOKA_COMPRESS_ENCODER)
    deflateEnd(&state->stream);
  else
    inflateEnd(&state->stream);
  state->initialized = 0;
}

static int ensure_output(TokaCompress *state, size_t needed) {
  size_t next_cap = state->output_cap;
  size_t max_cap = SIZE_MAX;
  if (state->direction == TOKA_COMPRESS_DECODER && state->output_limit != 0) {
    if (state->total_output > state->output_limit)
      return TOKA_COMPRESS_LIMIT;
    // One extra byte turns an attempted over-limit expansion into a bounded
    // deterministic error instead of an unbounded allocation.
    max_cap = state->output_limit - state->total_output;
    if (max_cap != SIZE_MAX)
      max_cap += 1;
  }
  if (needed > max_cap)
    needed = max_cap;
  if (needed <= state->output_cap)
    return TOKA_COMPRESS_OK;
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
    return TOKA_COMPRESS_LIMIT;
  unsigned char *next = realloc(state->output, next_cap);
  if (next == NULL)
    return TOKA_COMPRESS_OOM;
  state->output = next;
  state->output_cap = next_cap;
  return TOKA_COMPRESS_OK;
}

uintptr_t toka_compress_new(int direction, int format, int level,
                            size_t output_limit) {
  if ((direction != TOKA_COMPRESS_ENCODER && direction != TOKA_COMPRESS_DECODER) ||
      (format != TOKA_COMPRESS_GZIP && format != TOKA_COMPRESS_ZLIB) ||
      (direction == TOKA_COMPRESS_ENCODER && (level < -1 || level > 9)) ||
      (direction == TOKA_COMPRESS_DECODER && output_limit == 0))
    return 0;

  TokaCompress *state = calloc(1, sizeof(*state));
  if (state == NULL)
    return 0;
  state->direction = direction;
  state->output_limit = output_limit;
  int window_bits = format == TOKA_COMPRESS_GZIP ? 15 + 16 : 15;
  int status;
  if (direction == TOKA_COMPRESS_ENCODER)
    status = deflateInit2(&state->stream, level, Z_DEFLATED, window_bits,
                          8, Z_DEFAULT_STRATEGY);
  else
    status = inflateInit2(&state->stream, window_bits);
  if (status != Z_OK) {
    free(state);
    return 0;
  }
  state->initialized = 1;
  toka_compress_live_handles++;
  return (uintptr_t)state;
}

static void load_input(TokaCompress *state, const unsigned char **cursor,
                       size_t *remaining) {
  if (state->stream.avail_in != 0 || *remaining == 0)
    return;
  size_t chunk = *remaining > UINT_MAX ? UINT_MAX : *remaining;
  state->stream.next_in = (Bytef *)*cursor;
  state->stream.avail_in = (uInt)chunk;
  *cursor += chunk;
  *remaining -= chunk;
}

static int prepare_output(TokaCompress *state) {
  if (state->output_len == state->output_cap) {
    if (state->output_cap == SIZE_MAX)
      return TOKA_COMPRESS_LIMIT;
    int status = ensure_output(state, state->output_cap + 1);
    if (status != TOKA_COMPRESS_OK)
      return status;
  }
  size_t available = state->output_cap - state->output_len;
  if (available == 0)
    return TOKA_COMPRESS_LIMIT;
  if (available > UINT_MAX)
    available = UINT_MAX;
  state->stream.next_out = state->output + state->output_len;
  state->stream.avail_out = (uInt)available;
  return TOKA_COMPRESS_OK;
}

static int note_output(TokaCompress *state, uInt before) {
  state->output_len += (size_t)(before - state->stream.avail_out);
  if (state->direction == TOKA_COMPRESS_DECODER && state->output_limit != 0 &&
      state->output_len > state->output_limit - state->total_output)
    return TOKA_COMPRESS_LIMIT;
  return TOKA_COMPRESS_OK;
}

static int process_encoder(TokaCompress *state, const unsigned char *input,
                           size_t input_len, int finish) {
  const unsigned char *cursor = input;
  size_t remaining = input_len;
  while (1) {
    load_input(state, &cursor, &remaining);
    int prepared = prepare_output(state);
    if (prepared != TOKA_COMPRESS_OK)
      return prepared;
    uInt before = state->stream.avail_out;
    int status = deflate(&state->stream, finish ? Z_FINISH : Z_NO_FLUSH);
    int noted = note_output(state, before);
    if (noted != TOKA_COMPRESS_OK)
      return noted;
    if (status == Z_STREAM_END) {
      state->finished = 1;
      end_stream(state);
      return TOKA_COMPRESS_OK;
    }
    if (status != Z_OK)
      return TOKA_COMPRESS_ZLIB_ERROR;
    if (!finish && remaining == 0 && state->stream.avail_in == 0 &&
        state->stream.avail_out != 0)
      return TOKA_COMPRESS_OK;
  }
}

static int process_decoder(TokaCompress *state, const unsigned char *input,
                           size_t input_len, int finish) {
  const unsigned char *cursor = input;
  size_t remaining = input_len;
  while (1) {
    load_input(state, &cursor, &remaining);
    int prepared = prepare_output(state);
    if (prepared != TOKA_COMPRESS_OK)
      return prepared;
    uInt before = state->stream.avail_out;
    int status = inflate(&state->stream, finish ? Z_FINISH : Z_NO_FLUSH);
    int noted = note_output(state, before);
    if (noted != TOKA_COMPRESS_OK)
      return noted;
    if (status == Z_STREAM_END) {
      if (remaining != 0 || state->stream.avail_in != 0)
        return TOKA_COMPRESS_TRAILING;
      state->finished = 1;
      end_stream(state);
      return TOKA_COMPRESS_OK;
    }
    if (status == Z_BUF_ERROR) {
      if (finish)
        return TOKA_COMPRESS_TRUNCATED;
      if (remaining == 0 && state->stream.avail_in == 0)
        return TOKA_COMPRESS_OK;
      continue;
    }
    if (status != Z_OK)
      return TOKA_COMPRESS_ZLIB_ERROR;
    if (!finish && remaining == 0 && state->stream.avail_in == 0 &&
        state->stream.avail_out != 0)
      return TOKA_COMPRESS_OK;
  }
}

int toka_compress_process(uintptr_t raw, uintptr_t input_raw, size_t input_len,
                          int finish) {
  TokaCompress *state = (TokaCompress *)raw;
  if (state == NULL)
    return TOKA_COMPRESS_INVALID;
  if (state->finished)
    return finish && input_len == 0 ? TOKA_COMPRESS_OK : TOKA_COMPRESS_CLOSED;
  if (!state->initialized)
    return TOKA_COMPRESS_INVALID;
  if (input_len != 0 && input_raw == 0)
    return TOKA_COMPRESS_INVALID;

  clear_output(state);
  int status;
  if (state->direction == TOKA_COMPRESS_ENCODER)
    status = process_encoder(state, (const unsigned char *)input_raw, input_len, finish);
  else
    status = process_decoder(state, (const unsigned char *)input_raw, input_len, finish);
  if (status == TOKA_COMPRESS_OK) {
    state->total_output += state->output_len;
    return TOKA_COMPRESS_OK;
  }
  clear_output(state);
  return status;
}

size_t toka_compress_output_len(uintptr_t raw) {
  TokaCompress *state = (TokaCompress *)raw;
  return state == NULL ? 0 : state->output_len;
}

size_t toka_compress_output_cap(uintptr_t raw) {
  TokaCompress *state = (TokaCompress *)raw;
  return state == NULL ? 0 : state->output_cap;
}

uintptr_t toka_compress_take_output(uintptr_t raw) {
  TokaCompress *state = (TokaCompress *)raw;
  if (state == NULL)
    return 0;
  uintptr_t output = (uintptr_t)state->output;
  state->output = NULL;
  state->output_len = 0;
  state->output_cap = 0;
  return output;
}

void toka_compress_free(uintptr_t raw) {
  TokaCompress *state = (TokaCompress *)raw;
  if (state == NULL)
    return;
  clear_output(state);
  end_stream(state);
  free(state);
  toka_compress_live_handles--;
}

int toka_compress_live_handle_count(void) { return toka_compress_live_handles; }
