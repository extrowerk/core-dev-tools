#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "gcc.h"
#include "opts.h"

#if DEFAULT_STDLIB_LIBSTDCXX
#define DEFAULT_STDLIB_OPT OPT_stdlib_libstdc__
#elif DEFAULT_STDLIB_LIBCXX
#define DEFAULT_STDLIB_OPT OPT_stdlib_libc__
#endif

static size_t stdlib_opt = DEFAULT_STDLIB_OPT;

const char *
nto_select_libstdcxx (void)
{
  switch (stdlib_opt)
  {
    case OPT_stdlib_libcpp:
      return "cpp";
    case OPT_stdlib_libcpp_ne:
      return "cpp-ne";
    case OPT_stdlib_libc__:
      return "c++";
    case OPT_stdlib_libstdc__:
      return "stdc++";
    default:
      return NULL;
  }
}

const char *
nto_select_libstdcxx_static (void)
{
  switch (stdlib_opt)
  {
    case OPT_stdlib_libcpp:
    case OPT_stdlib_libcpp_ne:
      return "cxa";
    default:
      return NULL;
  }
}

void
nto_handle_cxx_option (size_t code,
		       const char *arg ATTRIBUTE_UNUSED)
{
  switch (code)
    {
    case OPT_stdlib_libc__:
    case OPT_stdlib_libstdc__:
    case OPT_stdlib_libcpp:
    case OPT_stdlib_libcpp_ne:
      stdlib_opt = code;
      break;
    }
}
