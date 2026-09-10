extern int raw_probe(int **key, int *value);
int run_raw_probe(void) {
    int key = 5;
    int *pointer = &key;
    int value = 9;
    return raw_probe(&pointer, &value);
}
