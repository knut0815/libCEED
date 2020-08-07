#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use std::mem;
use std::fmt;
use std::ffi::CString;
use std::io::prelude::*;
use std::io::{BufReader, SeekFrom};

use cfile;
// use std::io::{self, Write};

use crate::prelude::*;

/// CeedVector context wrapper
pub struct Vector<'a> {
  ceed_reference : &'a crate::ceed::Ceed,
  ceed_vec_ptr : rust_ceed::CeedVector,
}

impl<'a> Vector<'a> {
  pub fn new(ceed_reference: &'a crate::ceed::Ceed, ceed_vec_ptr: rust_ceed::CeedVector) -> Self {
    Self { ceed_reference , ceed_vec_ptr}
  }
}

/// Destructor
impl<'a> Drop for Vector<'a> {
  fn drop(&mut self) {
    unsafe {
      rust_ceed::CeedVectorDestroy(&mut self.ceed_vec_ptr);
    }
  }
}