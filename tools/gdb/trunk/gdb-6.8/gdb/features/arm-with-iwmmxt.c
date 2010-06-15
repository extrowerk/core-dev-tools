/* THIS FILE IS GENERATED.  Original: arm-with-iwmmxt.xml */

/* MUST BE INCLUDED AFTER arm-with-neon.c */

#include "defs.h"
#include "gdbtypes.h"
#include "target-descriptions.h"

struct target_desc *tdesc_arm_with_iwmmxt;
static void
initialize_tdesc_arm_with_iwmmxt (const int vfp_present)
{
  struct target_desc *result = 
	initialize_generic_target_desc (vfp_present);
  struct tdesc_feature *feature;
  struct type *field_type, *type;

  set_tdesc_architecture (result, bfd_scan_arch ("iwmmxt"));

  feature = tdesc_create_feature (result, "org.gnu.gdb.xscale.iwmmxt");
  field_type = tdesc_named_type (feature, "uint8");
  type = init_vector_type (field_type, 8);
  TYPE_NAME (type) = xstrdup ("iwmmxt_v8u8");
  tdesc_record_type (feature, type);

  field_type = tdesc_named_type (feature, "uint16");
  type = init_vector_type (field_type, 4);
  TYPE_NAME (type) = xstrdup ("iwmmxt_v4u16");
  tdesc_record_type (feature, type);

  field_type = tdesc_named_type (feature, "uint32");
  type = init_vector_type (field_type, 2);
  TYPE_NAME (type) = xstrdup ("iwmmxt_v2u32");
  tdesc_record_type (feature, type);

  type = init_composite_type (NULL, TYPE_CODE_UNION);
  TYPE_NAME (type) = xstrdup ("iwmmxt_vec64i");
  field_type = tdesc_named_type (feature, "iwmmxt_v8u8");
  append_composite_type_field (type, xstrdup ("u8"), field_type);
  field_type = tdesc_named_type (feature, "iwmmxt_v4u16");
  append_composite_type_field (type, xstrdup ("u16"), field_type);
  field_type = tdesc_named_type (feature, "iwmmxt_v2u32");
  append_composite_type_field (type, xstrdup ("u32"), field_type);
  field_type = tdesc_named_type (feature, "uint64");
  append_composite_type_field (type, xstrdup ("u64"), field_type);
  TYPE_FLAGS (type) |= TYPE_FLAG_VECTOR;
  tdesc_record_type (feature, type);

  tdesc_create_reg (feature, "wR0", 26, 1, NULL, 64, "iwmmxt_vec64i");
  tdesc_create_reg (feature, "wR1", 27, 1, NULL, 64, "iwmmxt_vec64i");
  tdesc_create_reg (feature, "wR2", 28, 1, NULL, 64, "iwmmxt_vec64i");
  tdesc_create_reg (feature, "wR3", 29, 1, NULL, 64, "iwmmxt_vec64i");
  tdesc_create_reg (feature, "wR4", 30, 1, NULL, 64, "iwmmxt_vec64i");
  tdesc_create_reg (feature, "wR5", 31, 1, NULL, 64, "iwmmxt_vec64i");
  tdesc_create_reg (feature, "wR6", 32, 1, NULL, 64, "iwmmxt_vec64i");
  tdesc_create_reg (feature, "wR7", 33, 1, NULL, 64, "iwmmxt_vec64i");
  tdesc_create_reg (feature, "wR8", 34, 1, NULL, 64, "iwmmxt_vec64i");
  tdesc_create_reg (feature, "wR9", 35, 1, NULL, 64, "iwmmxt_vec64i");
  tdesc_create_reg (feature, "wR10", 36, 1, NULL, 64, "iwmmxt_vec64i");
  tdesc_create_reg (feature, "wR11", 37, 1, NULL, 64, "iwmmxt_vec64i");
  tdesc_create_reg (feature, "wR12", 38, 1, NULL, 64, "iwmmxt_vec64i");
  tdesc_create_reg (feature, "wR13", 39, 1, NULL, 64, "iwmmxt_vec64i");
  tdesc_create_reg (feature, "wR14", 40, 1, NULL, 64, "iwmmxt_vec64i");
  tdesc_create_reg (feature, "wR15", 41, 1, NULL, 64, "iwmmxt_vec64i");
  tdesc_create_reg (feature, "wCSSF", 42, 1, "vector", 32, "int");
  tdesc_create_reg (feature, "wCASF", 43, 1, "vector", 32, "int");
  tdesc_create_reg (feature, "wCGR0", 44, 1, "vector", 32, "int");
  tdesc_create_reg (feature, "wCGR1", 45, 1, "vector", 32, "int");
  tdesc_create_reg (feature, "wCGR2", 46, 1, "vector", 32, "int");
  tdesc_create_reg (feature, "wCGR3", 47, 1, "vector", 32, "int");

  tdesc_arm_with_iwmmxt = result;
}
