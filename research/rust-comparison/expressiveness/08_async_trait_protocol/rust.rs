use std::future::Future;
use std::pin::pin;
use std::sync::Arc;
use std::task::{Context, Poll, Wake, Waker};

struct Noop;

impl Wake for Noop {
    fn wake(self: Arc<Self>) {}
}

fn block_on<F: Future>(future: F) -> F::Output {
    let waker = Waker::from(Arc::new(Noop));
    let mut context = Context::from_waker(&waker);
    let mut future = pin!(future);
    loop {
        match future.as_mut().poll(&mut context) {
            Poll::Ready(value) => return value,
            Poll::Pending => std::thread::yield_now(),
        }
    }
}

trait Fetcher {
    async fn fetch(&self) -> i32;
}

struct Immediate;

impl Fetcher for Immediate {
    async fn fetch(&self) -> i32 {
        17
    }
}

fn consume<F: Fetcher>(fetcher: &F) -> i32 {
    block_on(fetcher.fetch())
}

fn main() {
    assert_eq!(consume(&Immediate), 17);
}
