#include <stdint.h>
#include <stdlib.h>

int32_t toka_take_cell(void *cell) {
  int32_t value = *(const int32_t *)cell;
  free(cell);
  return value;
}

int32_t toka_take_two_cells(void *left, void *right) {
  int32_t value = *(const int32_t *)left + *(const int32_t *)right;
  free(left);
  free(right);
  return value;
}
