/* THIS FILE IS GENERATED manually by using arm-with-neon.c as a base.

  A change in feature_to_c.sh made auto-generation impossible; we don't
 want to use xml parser so we have to hard code new target descriptions
 like this. */ 

#include "defs.h"
#include "gdbtypes.h"
#include "target-descriptions.h"
#include "reggroups.h"

extern struct target_desc *tdesc_arm_with_neon;
struct target_desc *tdesc_arm_with_vfp;


static struct target_desc *
initialize_generic_target_desc (const int vfp_present)
{
  struct target_desc *result = allocate_target_description ();
  struct tdesc_feature *feature;

  set_tdesc_architecture (result, bfd_scan_arch ("neon"));

  feature = tdesc_create_feature (result, "org.gnu.gdb.arm.core");
  tdesc_create_reg (feature, "r0", 0, 1, NULL, 32, "int");
  tdesc_create_reg (feature, "r1", 1, 1, NULL, 32, "int");
  tdesc_create_reg (feature, "r2", 2, 1, NULL, 32, "int");
  tdesc_create_reg (feature, "r3", 3, 1, NULL, 32, "int");
  tdesc_create_reg (feature, "r4", 4, 1, NULL, 32, "int");
  tdesc_create_reg (feature, "r5", 5, 1, NULL, 32, "int");
  tdesc_create_reg (feature, "r6", 6, 1, NULL, 32, "int");
  tdesc_create_reg (feature, "r7", 7, 1, NULL, 32, "int");
  tdesc_create_reg (feature, "r8", 8, 1, NULL, 32, "int");
  tdesc_create_reg (feature, "r9", 9, 1, NULL, 32, "int");
  tdesc_create_reg (feature, "r10", 10, 1, NULL, 32, "int");
  tdesc_create_reg (feature, "r11", 11, 1, NULL, 32, "int");
  tdesc_create_reg (feature, "r12", 12, 1, NULL, 32, "int");
  tdesc_create_reg (feature, "sp", 13, 1, NULL, 32, "data_ptr");
  tdesc_create_reg (feature, "lr", 14, 1, NULL, 32, "int");
  tdesc_create_reg (feature, "pc", 15, 1, NULL, 32, "code_ptr");
  tdesc_create_reg (feature, "cpsr", 25, 1, NULL, 32, "int");

  if (vfp_present)
    {
      feature = tdesc_create_feature (result, "org.gnu.gdb.arm.vfp");

      /* Add vfp register descriptions. */

      tdesc_create_reg (feature, "d0", ARM_D0_REGNUM + 0, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d1", ARM_D0_REGNUM + 1, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d2", ARM_D0_REGNUM + 2, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d3", ARM_D0_REGNUM + 3, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d4", ARM_D0_REGNUM + 4, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d5", ARM_D0_REGNUM + 5, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d6", ARM_D0_REGNUM + 6, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d7", ARM_D0_REGNUM + 7, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d8", ARM_D0_REGNUM + 8, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d9", ARM_D0_REGNUM + 9, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d10", ARM_D0_REGNUM + 10, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d11", ARM_D0_REGNUM + 11, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d12", ARM_D0_REGNUM + 12, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d13", ARM_D0_REGNUM + 13, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d14", ARM_D0_REGNUM + 14, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d15", ARM_D0_REGNUM + 15, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d16", ARM_D0_REGNUM + 16, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d17", ARM_D0_REGNUM + 17, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d18", ARM_D0_REGNUM + 18, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d19", ARM_D0_REGNUM + 19, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d20", ARM_D0_REGNUM + 20, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d21", ARM_D0_REGNUM + 21, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d22", ARM_D0_REGNUM + 22, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d23", ARM_D0_REGNUM + 23, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d24", ARM_D0_REGNUM + 24, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d25", ARM_D0_REGNUM + 25, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d26", ARM_D0_REGNUM + 26, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d27", ARM_D0_REGNUM + 27, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d28", ARM_D0_REGNUM + 28, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d29", ARM_D0_REGNUM + 29, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d30", ARM_D0_REGNUM + 30, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "d31", ARM_D0_REGNUM + 31, 1, NULL, 64, "float");
      tdesc_create_reg (feature, "fpscr", 58, 1, "float", 32, "int");
    }

  return result;
}

static void
initialize_tdesc_arm_with_vfp ()
{
  struct target_desc *result =
	initialize_generic_target_desc (1);

  tdesc_arm_with_vfp = result;
}

static void
initialize_tdesc_arm_with_neon (const int vfp_present)
{
  struct target_desc *result =
	initialize_generic_target_desc (vfp_present);
  struct tdesc_feature *feature;

  /* Add neon feature. No extra registers are defined, we will use VFP regs
     and pseudo registers will be added by arm-tdep.c  */
  feature = tdesc_create_feature (result, "org.gnu.gdb.arm.neon");

  tdesc_arm_with_neon = result;
}
