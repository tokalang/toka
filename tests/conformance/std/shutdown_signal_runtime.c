#include <assert.h>

int toka_shutdown_signal_install(void);
int toka_shutdown_signal_take(void);
int toka_shutdown_signal_raise_for_test(int signal_number);
int toka_shutdown_signal_supported(void);

int main(void) {
    if (!toka_shutdown_signal_supported()) return 0;
    assert(toka_shutdown_signal_install() == 0);
    assert(toka_shutdown_signal_take() == 0);
    assert(toka_shutdown_signal_raise_for_test(15) == 0);
    assert(toka_shutdown_signal_raise_for_test(2) == 0);
    assert(toka_shutdown_signal_take() == 15);
    assert(toka_shutdown_signal_take() == 0);
    return 0;
}
