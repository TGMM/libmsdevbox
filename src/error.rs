//! Thread-local last-error reporting for the C ABI: fallible FFI calls set
//! it, `msdevbox_last_error()` reads it.

use std::cell::RefCell;

thread_local! {
    static LAST_ERROR: RefCell<String> = RefCell::new(String::new());
}

/// Record the error for the current thread; used before returning a
/// failure sentinel from an FFI call.
pub fn set_last_error<E: ToString>(err: E) {
    LAST_ERROR.with(|slot| *slot.borrow_mut() = err.to_string());
}

/// The last error recorded on this thread (empty when the last call succeeded).
pub fn last_error() -> String {
    LAST_ERROR.with(|slot| slot.borrow().clone())
}

/// Set the error and return `false`, for use in `map_err` chains.
pub fn fail<T, E: ToString>(err: E) -> Result<T, String> {
    let text = err.to_string();
    set_last_error(text.clone());
    Err(text)
}
