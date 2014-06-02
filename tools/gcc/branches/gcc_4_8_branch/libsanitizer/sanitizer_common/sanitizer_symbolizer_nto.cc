//===-- sanitizer_symbolizer_nto.cc ---------------------------------------===//
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries.
// Neutrino-specific implementation of symbolizer parts.
//===----------------------------------------------------------------------===//
#ifdef __QNXNTO__
#include "sanitizer_internal_defs.h"
#include "sanitizer_symbolizer.h"

namespace __sanitizer {

bool StartSymbolizerSubprocess(const char *path_to_symbolizer,
                               int *input_fd, int *output_fd) {
  UNIMPLEMENTED();
}

uptr GetListOfModules(LoadedModule *modules, uptr max_modules) {
  UNIMPLEMENTED();
}

}  // namespace __sanitizer

#endif  // __QNXNTO__

