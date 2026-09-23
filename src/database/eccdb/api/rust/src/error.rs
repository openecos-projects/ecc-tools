use std::ffi::{CStr, NulError};
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ErrorKind {
    NotFound,
    InvalidArgument,
    InvalidState,
    Io,
    Internal,
    InvalidUtf8,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Error {
    pub kind: ErrorKind,
    pub message: String,
}
pub type Result<T> = std::result::Result<T, Error>;
impl Error {
    pub(crate) fn argument(message: impl Into<String>) -> Self {
        Self {
            kind: ErrorKind::InvalidArgument,
            message: message.into(),
        }
    }
}
impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}: {}", self.kind, self.message)
    }
}
impl std::error::Error for Error {}
impl From<NulError> for Error {
    fn from(e: NulError) -> Self {
        Self::argument(e.to_string())
    }
}
pub(crate) fn check(status: i32) -> Result<()> {
    if status == 0 {
        return Ok(());
    }
    let message = unsafe { CStr::from_ptr(crate::ffi::eccdb_last_error_message()) }
        .to_string_lossy()
        .into_owned();
    Err(Error {
        kind: match status {
            1 => ErrorKind::NotFound,
            2 => ErrorKind::InvalidArgument,
            3 => ErrorKind::InvalidState,
            4 => ErrorKind::Io,
            _ => ErrorKind::Internal,
        },
        message,
    })
}
