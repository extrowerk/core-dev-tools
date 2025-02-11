// aarch64.cc -- aarch64 target support for gold.

// Copyright (C) 2014 Free Software Foundation, Inc.
// Written by Jing Yu <jingyu@google.com> and Han Shen <shenhan@google.com>.

// This file is part of gold.

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
// MA 02110-1301, USA.

#include "gold.h"

#include <cstring>

#include "elfcpp.h"
#include "dwarf.h"
#include "parameters.h"
#include "reloc.h"
#include "aarch64.h"
#include "object.h"
#include "symtab.h"
#include "layout.h"
#include "output.h"
#include "copy-relocs.h"
#include "target.h"
#include "target-reloc.h"
#include "target-select.h"
#include "tls.h"
#include "freebsd.h"
#include "nacl.h"
#include "gc.h"
#include "icf.h"
#include "aarch64-reloc-property.h"

// The first three .got.plt entries are reserved.
const int32_t AARCH64_GOTPLT_RESERVE_COUNT = 3;


namespace
{

using namespace gold;

template<int size, bool big_endian>
class Output_data_plt_aarch64;

template<int size, bool big_endian>
class Output_data_plt_aarch64_standard;

template<int size, bool big_endian>
class Target_aarch64;

template<int size, bool big_endian>
class AArch64_relocate_functions;

// Output_data_got_aarch64 class.

template<int size, bool big_endian>
class Output_data_got_aarch64 : public Output_data_got<size, big_endian>
{
 public:
  typedef typename elfcpp::Elf_types<size>::Elf_Addr Valtype;
  Output_data_got_aarch64(Symbol_table* symtab, Layout* layout)
    : Output_data_got<size, big_endian>(),
      symbol_table_(symtab), layout_(layout)
  { }

  // Add a static entry for the GOT entry at OFFSET.  GSYM is a global
  // symbol and R_TYPE is the code of a dynamic relocation that needs to be
  // applied in a static link.
  void
  add_static_reloc(unsigned int got_offset, unsigned int r_type, Symbol* gsym)
  { this->static_relocs_.push_back(Static_reloc(got_offset, r_type, gsym)); }


  // Add a static reloc for the GOT entry at OFFSET.  RELOBJ is an object
  // defining a local symbol with INDEX.  R_TYPE is the code of a dynamic
  // relocation that needs to be applied in a static link.
  void
  add_static_reloc(unsigned int got_offset, unsigned int r_type,
		   Sized_relobj_file<size, big_endian>* relobj,
		   unsigned int index)
  {
    this->static_relocs_.push_back(Static_reloc(got_offset, r_type, relobj,
						index));
  }


 protected:
  // Write out the GOT table.
  void
  do_write(Output_file* of) {
    // The first entry in the GOT is the address of the .dynamic section.
    gold_assert(this->data_size() >= size / 8);
    Output_section* dynamic = this->layout_->dynamic_section();
    Valtype dynamic_addr = dynamic == NULL ? 0 : dynamic->address();
    this->replace_constant(0, dynamic_addr);
    Output_data_got<size, big_endian>::do_write(of);

    // Handling static relocs
    if (this->static_relocs_.empty())
      return;

    typedef typename elfcpp::Elf_types<size>::Elf_Addr AArch64_address;

    gold_assert(parameters->doing_static_link());
    const off_t offset = this->offset();
    const section_size_type oview_size =
      convert_to_section_size_type(this->data_size());
    unsigned char* const oview = of->get_output_view(offset, oview_size);

    Output_segment* tls_segment = this->layout_->tls_segment();
    gold_assert(tls_segment != NULL);

    AArch64_address aligned_tcb_address =
      align_address(Target_aarch64<size, big_endian>::TCB_SIZE,
		    tls_segment->maximum_alignment());

    for (size_t i = 0; i < this->static_relocs_.size(); ++i)
      {
	Static_reloc& reloc(this->static_relocs_[i]);
	AArch64_address value;

	if (!reloc.symbol_is_global())
	  {
	    Sized_relobj_file<size, big_endian>* object = reloc.relobj();
	    const Symbol_value<size>* psymval =
	      reloc.relobj()->local_symbol(reloc.index());

	    // We are doing static linking.  Issue an error and skip this
	    // relocation if the symbol is undefined or in a discarded_section.
	    bool is_ordinary;
	    unsigned int shndx = psymval->input_shndx(&is_ordinary);
	    if ((shndx == elfcpp::SHN_UNDEF)
		|| (is_ordinary
		    && shndx != elfcpp::SHN_UNDEF
		    && !object->is_section_included(shndx)
		    && !this->symbol_table_->is_section_folded(object, shndx)))
	      {
		gold_error(_("undefined or discarded local symbol %u from "
			     " object %s in GOT"),
			   reloc.index(), reloc.relobj()->name().c_str());
		continue;
	      }
	    value = psymval->value(object, 0);
	  }
	else
	  {
	    const Symbol* gsym = reloc.symbol();
	    gold_assert(gsym != NULL);
	    if (gsym->is_forwarder())
	      gsym = this->symbol_table_->resolve_forwards(gsym);

	    // We are doing static linking.  Issue an error and skip this
	    // relocation if the symbol is undefined or in a discarded_section
	    // unless it is a weakly_undefined symbol.
	    if ((gsym->is_defined_in_discarded_section()
		 || gsym->is_undefined())
		&& !gsym->is_weak_undefined())
	      {
		gold_error(_("undefined or discarded symbol %s in GOT"),
			   gsym->name());
		continue;
	      }

	    if (!gsym->is_weak_undefined())
	      {
		const Sized_symbol<size>* sym =
		  static_cast<const Sized_symbol<size>*>(gsym);
		value = sym->value();
	      }
	    else
	      value = 0;
	  }

	unsigned got_offset = reloc.got_offset();
	gold_assert(got_offset < oview_size);

	typedef typename elfcpp::Swap<size, big_endian>::Valtype Valtype;
	Valtype* wv = reinterpret_cast<Valtype*>(oview + got_offset);
	Valtype x;
	switch (reloc.r_type())
	  {
	  case elfcpp::R_AARCH64_TLS_DTPREL64:
	    x = value;
	    break;
	  case elfcpp::R_AARCH64_TLS_TPREL64:
	    x = value + aligned_tcb_address;
	    break;
	  default:
	    gold_unreachable();
	  }
	elfcpp::Swap<size, big_endian>::writeval(wv, x);
      }

    of->write_output_view(offset, oview_size, oview);
  }

 private:
  // Symbol table of the output object.
  Symbol_table* symbol_table_;
  // A pointer to the Layout class, so that we can find the .dynamic
  // section when we write out the GOT section.
  Layout* layout_;

  // This class represent dynamic relocations that need to be applied by
  // gold because we are using TLS relocations in a static link.
  class Static_reloc
  {
   public:
    Static_reloc(unsigned int got_offset, unsigned int r_type, Symbol* gsym)
      : got_offset_(got_offset), r_type_(r_type), symbol_is_global_(true)
    { this->u_.global.symbol = gsym; }

    Static_reloc(unsigned int got_offset, unsigned int r_type,
	  Sized_relobj_file<size, big_endian>* relobj, unsigned int index)
      : got_offset_(got_offset), r_type_(r_type), symbol_is_global_(false)
    {
      this->u_.local.relobj = relobj;
      this->u_.local.index = index;
    }

    // Return the GOT offset.
    unsigned int
    got_offset() const
    { return this->got_offset_; }

    // Relocation type.
    unsigned int
    r_type() const
    { return this->r_type_; }

    // Whether the symbol is global or not.
    bool
    symbol_is_global() const
    { return this->symbol_is_global_; }

    // For a relocation against a global symbol, the global symbol.
    Symbol*
    symbol() const
    {
      gold_assert(this->symbol_is_global_);
      return this->u_.global.symbol;
    }

    // For a relocation against a local symbol, the defining object.
    Sized_relobj_file<size, big_endian>*
    relobj() const
    {
      gold_assert(!this->symbol_is_global_);
      return this->u_.local.relobj;
    }

    // For a relocation against a local symbol, the local symbol index.
    unsigned int
    index() const
    {
      gold_assert(!this->symbol_is_global_);
      return this->u_.local.index;
    }

   private:
    // GOT offset of the entry to which this relocation is applied.
    unsigned int got_offset_;
    // Type of relocation.
    unsigned int r_type_;
    // Whether this relocation is against a global symbol.
    bool symbol_is_global_;
    // A global or local symbol.
    union
    {
      struct
      {
	// For a global symbol, the symbol itself.
	Symbol* symbol;
      } global;
      struct
      {
	// For a local symbol, the object defining the symbol.
	Sized_relobj_file<size, big_endian>* relobj;
	// For a local symbol, the symbol index.
	unsigned int index;
      } local;
    } u_;
  };  // End of inner class Static_reloc

  std::vector<Static_reloc> static_relocs_;
};  // End of Output_data_got_aarch64


template<int size, bool big_endian>
class AArch64_input_section;


template<int size, bool big_endian>
class AArch64_output_section;


// Reloc stub class.

template<int size, bool big_endian>
class Reloc_stub
{
 public:
  typedef Reloc_stub<size, big_endian> This;
  typedef typename elfcpp::Elf_types<size>::Elf_Addr AArch64_address;

  // Do not change the value of the enums, they are used to index into
  // stub_insns array.
  typedef enum
  {
    ST_NONE = 0,

    // Using adrp/add pair, 4 insns (including alignment) without mem access,
    // the fastest stub. This has a limited jump distance, which is tested by
    // aarch64_valid_for_adrp_p.
    ST_ADRP_BRANCH = 1,

    // Using ldr-absolute-address/br-register, 4 insns with 1 mem access,
    // unlimited in jump distance.
    ST_LONG_BRANCH_ABS = 2,

    // Using ldr/calculate-pcrel/jump, 8 insns (including alignment) with 1 mem
    // access, slowest one. Only used in position independent executables.
    ST_LONG_BRANCH_PCREL = 3,

  } Stub_type;

  // Branch range. This is used to calculate the section group size, as well as
  // determine whether a stub is needed.
  static const int MAX_BRANCH_OFFSET = ((1 << 25) - 1) << 2;
  static const int MIN_BRANCH_OFFSET = -((1 << 25) << 2);

  // Constant used to determine if an offset fits in the adrp instruction
  // encoding.
  static const int MAX_ADRP_IMM = (1 << 20) - 1;
  static const int MIN_ADRP_IMM = -(1 << 20);

  static const int BYTES_PER_INSN = 4;
  static const int STUB_ADDR_ALIGN = 4;

  // Determine whether the offset fits in the jump/branch instruction.
  static bool
  aarch64_valid_branch_offset_p(int64_t offset)
  { return offset >= MIN_BRANCH_OFFSET && offset <= MAX_BRANCH_OFFSET; }

  // Determine whether the offset fits in the adrp immediate field.
  static bool
  aarch64_valid_for_adrp_p(AArch64_address location, AArch64_address dest)
  {
    typedef AArch64_relocate_functions<size, big_endian> Reloc;
    int64_t adrp_imm = (Reloc::Page(dest) - Reloc::Page(location)) >> 12;
    return adrp_imm >= MIN_ADRP_IMM && adrp_imm <= MAX_ADRP_IMM;
  }

  // Determine the stub type for a certain relocation or ST_NONE, if no stub is
  // needed.
  static Stub_type
  stub_type_for_reloc(unsigned int r_type, AArch64_address address,
		      AArch64_address target);

  Reloc_stub(Stub_type stub_type)
    : stub_type_(stub_type), offset_(invalid_offset),
      destination_address_(invalid_address)
  { }

  ~Reloc_stub()
  { }

  // Return offset of code stub from beginning of its containing stub table.
  section_offset_type
  offset() const
  {
    gold_assert(this->offset_ != invalid_offset);
    return this->offset_;
  }

  // Set offset of code stub from beginning of its containing stub table.
  void
  set_offset(section_offset_type offset)
  { this->offset_ = offset; }

  // Return destination address.
  AArch64_address
  destination_address() const
  {
    gold_assert(this->destination_address_ != this->invalid_address);
    return this->destination_address_;
  }

  // Set destination address.
  void
  set_destination_address(AArch64_address address)
  {
    gold_assert(address != this->invalid_address);
    this->destination_address_ = address;
  }

  // Reset the destination address.
  void
  reset_destination_address()
  { this->destination_address_ = this->invalid_address; }

  // Return the stub type.
  Stub_type
  stub_type() const
  { return stub_type_; }

  // Return the stub size.
  uint32_t
  stub_size() const
  { return this->stub_insn_number() * BYTES_PER_INSN; }

  // Return the instruction number of this stub instance.
  int
  stub_insn_number() const
  { return stub_insns_[this->stub_type_][0]; }

  // Note the first "insn" is the number of total insns in this array.
  const uint32_t*
  stub_insns() const
  { return stub_insns_[this->stub_type_]; }

  // Write stub to output file.
  void
  write(unsigned char* view, section_size_type view_size)
  { this->do_write(view, view_size); }

  // The key class used to index the stub instance in the stub table's stub map.
  class Key
  {
   public:
    Key(Stub_type stub_type, const Symbol* symbol, const Relobj* relobj,
	unsigned int r_sym, int32_t addend)
      : stub_type_(stub_type), addend_(addend)
    {
      if (symbol != NULL)
	{
	  this->r_sym_ = Reloc_stub::invalid_index;
	  this->u_.symbol = symbol;
	}
      else
	{
	  gold_assert(relobj != NULL && r_sym != invalid_index);
	  this->r_sym_ = r_sym;
	  this->u_.relobj = relobj;
	}
    }

    ~Key()
    { }

    // Return stub type.
    Stub_type
    stub_type() const
    { return this->stub_type_; }

    // Return the local symbol index or invalid_index.
    unsigned int
    r_sym() const
    { return this->r_sym_; }

    // Return the symbol if there is one.
    const Symbol*
    symbol() const
    { return this->r_sym_ == invalid_index ? this->u_.symbol : NULL; }

    // Return the relobj if there is one.
    const Relobj*
    relobj() const
    { return this->r_sym_ != invalid_index ? this->u_.relobj : NULL; }

    // Whether this equals to another key k.
    bool
    eq(const Key& k) const
    {
      return ((this->stub_type_ == k.stub_type_)
	      && (this->r_sym_ == k.r_sym_)
	      && ((this->r_sym_ != Reloc_stub::invalid_index)
		  ? (this->u_.relobj == k.u_.relobj)
		  : (this->u_.symbol == k.u_.symbol))
	      && (this->addend_ == k.addend_));
    }

    // Return a hash value.
    size_t
    hash_value() const
    {
      size_t name_hash_value = gold::string_hash<char>(
	  (this->r_sym_ != Reloc_stub::invalid_index)
	  ? this->u_.relobj->name().c_str()
	  : this->u_.symbol->name());
      // We only have 4 stub types.
      size_t stub_type_hash_value = 0x03 & this->stub_type_;
      return (name_hash_value
	      ^ stub_type_hash_value
	      ^ ((this->r_sym_ & 0x3fff) << 2)
	      ^ ((this->addend_ & 0xffff) << 16));
    }

    // Functors for STL associative containers.
    struct hash
    {
      size_t
      operator()(const Key& k) const
      { return k.hash_value(); }
    };

    struct equal_to
    {
      bool
      operator()(const Key& k1, const Key& k2) const
      { return k1.eq(k2); }
    };

   private:
    // Stub type.
    const Stub_type stub_type_;
    // If this is a local symbol, this is the index in the defining object.
    // Otherwise, it is invalid_index for a global symbol.
    unsigned int r_sym_;
    // If r_sym_ is an invalid index, this points to a global symbol.
    // Otherwise, it points to a relobj.  We used the unsized and target
    // independent Symbol and Relobj classes instead of Sized_symbol<32> and
    // Arm_relobj, in order to avoid making the stub class a template
    // as most of the stub machinery is endianness-neutral.  However, it
    // may require a bit of casting done by users of this class.
    union
    {
      const Symbol* symbol;
      const Relobj* relobj;
    } u_;
    // Addend associated with a reloc.
    int32_t addend_;
  };  // End of inner class Reloc_stub::Key

 protected:
  // This may be overridden in the child class.
  virtual void
  do_write(unsigned char*, section_size_type);

 private:
  static const section_offset_type invalid_offset =
      static_cast<section_offset_type>(-1);
  static const unsigned int invalid_index = static_cast<unsigned int>(-1);
  static const AArch64_address invalid_address =
      static_cast<AArch64_address>(-1);

  static const uint32_t stub_insns_[][10];

  const Stub_type stub_type_;
  section_offset_type offset_;
  AArch64_address destination_address_;
};  // End of Reloc_stub


// Write data to output file.

template<int size, bool big_endian>
void
Reloc_stub<size, big_endian>::
do_write(unsigned char* view, section_size_type)
{
  typedef typename elfcpp::Swap<32, big_endian>::Valtype Insntype;
  const uint32_t* insns = this->stub_insns();
  uint32_t num_insns = this->stub_insn_number();
  Insntype* ip = reinterpret_cast<Insntype*>(view);
  for (uint32_t i = 1; i <= num_insns; ++i)
    elfcpp::Swap<32, big_endian>::writeval(ip + i - 1, insns[i]);
}


// Stubs instructions definition.

template<int size, bool big_endian>
const uint32_t
Reloc_stub<size, big_endian>::stub_insns_[][10] =
  {
    // The first element of each group is the num of the insns.

    // ST_NONE
    {0, 0},

    // ST_ADRP_BRANCH
    {
	4,
	0x90000010,	/*	adrp	ip0, X		   */
			/*	  ADR_PREL_PG_HI21(X)	   */
	0x91000210,	/*	add	ip0, ip0, :lo12:X  */
			/*	  ADD_ABS_LO12_NC(X)	   */
	0xd61f0200,	/*	br	ip0		   */
	0x00000000,	/*	alignment padding	   */
    },

    // ST_LONG_BRANCH_ABS
    {
	4,
	0x58000050,	/*	ldr   ip0, 0x8		   */
	0xd61f0200,	/*	br    ip0		   */
	0x00000000,	/*	address field		   */
	0x00000000,	/*	address fields		   */
    },

    // ST_LONG_BRANCH_PCREL
    {
      8,
	0x58000090,	/*	ldr   ip0, 0x10            */
	0x10000011,	/*	adr   ip1, #0		   */
	0x8b110210,	/*	add   ip0, ip0, ip1	   */
	0xd61f0200,	/*	br    ip0		   */
	0x00000000,	/*	address field		   */
	0x00000000,	/*	address field		   */
	0x00000000,	/*	alignment padding	   */
	0x00000000,	/*	alignment padding	   */
    }
  };


// Determine the stub type for a certain relocation or ST_NONE, if no stub is
// needed.

template<int size, bool big_endian>
inline
typename Reloc_stub<size, big_endian>::Stub_type
Reloc_stub<size, big_endian>::stub_type_for_reloc(
    unsigned int r_type, AArch64_address location, AArch64_address dest)
{
  int64_t branch_offset = 0;
  switch(r_type)
    {
    case elfcpp::R_AARCH64_CALL26:
    case elfcpp::R_AARCH64_JUMP26:
      branch_offset = dest - location;
      break;
    default:
      gold_unreachable();
    }

  if (aarch64_valid_branch_offset_p(branch_offset))
    return ST_NONE;

  if (aarch64_valid_for_adrp_p(location, dest))
    return ST_ADRP_BRANCH;

  if (parameters->options().output_is_position_independent()
      && parameters->options().output_is_executable())
    return ST_LONG_BRANCH_PCREL;

  return ST_LONG_BRANCH_ABS;
}

// A class to hold stubs for the ARM target.

template<int size, bool big_endian>
class Stub_table : public Output_data
{
 public:
  typedef Target_aarch64<size, big_endian> The_target_aarch64;
  typedef typename elfcpp::Elf_types<size>::Elf_Addr AArch64_address;
  typedef AArch64_input_section<size, big_endian> The_aarch64_input_section;
  typedef Reloc_stub<size, big_endian> The_reloc_stub;
  typedef typename The_reloc_stub::Key The_reloc_stub_key;
  typedef typename The_reloc_stub_key::hash The_reloc_stub_key_hash;
  typedef typename The_reloc_stub_key::equal_to The_reloc_stub_key_equal_to;
  typedef Stub_table<size, big_endian> The_stub_table;
  typedef Unordered_map<The_reloc_stub_key, The_reloc_stub*,
			The_reloc_stub_key_hash, The_reloc_stub_key_equal_to>
			Reloc_stub_map;
  typedef typename Reloc_stub_map::const_iterator Reloc_stub_map_const_iter;
  typedef Relocate_info<size, big_endian> The_relocate_info;

  Stub_table(The_aarch64_input_section* owner)
    : Output_data(), owner_(owner), reloc_stubs_size_(0), prev_data_size_(0)
  { }

  ~Stub_table()
  { }

  The_aarch64_input_section*
  owner() const
  { return owner_; }

  // Whether this stub table is empty.
  bool
  empty() const
  { return reloc_stubs_.empty(); }

  // Return the current data size.
  off_t
  current_data_size() const
  { return this->current_data_size_for_child(); }

  // Add a STUB using KEY.  The caller is responsible for avoiding addition
  // if a STUB with the same key has already been added.
  void
  add_reloc_stub(The_reloc_stub* stub, const The_reloc_stub_key& key);

  // Finalize stubs. No-op here, just for completeness.
  void
  finalize_stubs()
  { }

  // Look up a relocation stub using KEY. Return NULL if there is none.
  The_reloc_stub*
  find_reloc_stub(The_reloc_stub_key& key)
  {
    Reloc_stub_map_const_iter p = this->reloc_stubs_.find(key);
    return (p != this->reloc_stubs_.end()) ? p->second : NULL;
  }

  // Relocate stubs in this stub table.
  void
  relocate_stubs(const The_relocate_info*,
		 The_target_aarch64*,
		 Output_section*,
		 unsigned char*,
		 AArch64_address,
		 section_size_type);

  // Update data size at the end of a relaxation pass.  Return true if data size
  // is different from that of the previous relaxation pass.
  bool
  update_data_size_changed_p()
  {
    // No addralign changed here.
    off_t s = this->reloc_stubs_size_;
    bool changed = (s != this->prev_data_size_);
    this->prev_data_size_ = s;
    return changed;
  }

 protected:
  // Write out section contents.
  void
  do_write(Output_file*);

  // Return the required alignment.
  uint64_t
  do_addralign() const
  { return The_reloc_stub::STUB_ADDR_ALIGN; }

  // Reset address and file offset.
  void
  do_reset_address_and_file_offset()
  { this->set_current_data_size_for_child(this->prev_data_size_); }

  // Set final data size.
  void
  set_final_data_size()
  { this->set_data_size(this->current_data_size()); }

 private:
  // Relocate one stub.
  void
  relocate_stub(The_reloc_stub*,
		const The_relocate_info*,
		The_target_aarch64*,
		Output_section*,
		unsigned char*,
		AArch64_address,
		section_size_type);

 private:
  // Owner of this stub table.
  The_aarch64_input_section* owner_;
  // The relocation stubs.
  Reloc_stub_map reloc_stubs_;
  // Size of reloc stubs.
  off_t reloc_stubs_size_;
  // data size of this in the previous pass.
  off_t prev_data_size_;
};  // End of Stub_table


// Add a STUB using KEY.  The caller is responsible for avoiding addition
// if a STUB with the same key has already been added.

template<int size, bool big_endian>
void
Stub_table<size, big_endian>::add_reloc_stub(
    The_reloc_stub* stub, const The_reloc_stub_key& key)
{
  gold_assert(stub->stub_type() == key.stub_type());
  this->reloc_stubs_[key] = stub;

  // Assign stub offset early.  We can do this because we never remove
  // reloc stubs and they are in the beginning of the stub table.
  this->reloc_stubs_size_ = align_address(this->reloc_stubs_size_,
					  The_reloc_stub::STUB_ADDR_ALIGN);
  stub->set_offset(this->reloc_stubs_size_);
  this->reloc_stubs_size_ += stub->stub_size();
}


// Relocate all stubs in this stub table.

template<int size, bool big_endian>
void
Stub_table<size, big_endian>::
relocate_stubs(const The_relocate_info* relinfo,
	       The_target_aarch64* target_aarch64,
	       Output_section* output_section,
	       unsigned char* view,
	       AArch64_address address,
	       section_size_type view_size)
{
  // "view_size" is the total size of the stub_table.
  gold_assert(address == this->address() &&
	      view_size == static_cast<section_size_type>(this->data_size()));
  for(Reloc_stub_map_const_iter p = this->reloc_stubs_.begin();
      p != this->reloc_stubs_.end(); ++p)
    relocate_stub(p->second, relinfo, target_aarch64, output_section,
		  view, address, view_size);
}


// Relocate one stub.  This is a helper for Stub_table::relocate_stubs().

template<int size, bool big_endian>
void
Stub_table<size, big_endian>::
relocate_stub(The_reloc_stub* stub,
	      const The_relocate_info* relinfo,
	      The_target_aarch64* target_aarch64,
	      Output_section* output_section,
	      unsigned char* view,
	      AArch64_address address,
	      section_size_type view_size)
{
  // "offset" is the offset from the beginning of the stub_table.
  section_size_type offset = stub->offset();
  section_size_type stub_size = stub->stub_size();
  // "view_size" is the total size of the stub_table.
  gold_assert(offset + stub_size <= view_size);

  target_aarch64->relocate_stub(stub, relinfo, output_section,
				view + offset, address + offset, view_size);
}


// Write out the stubs to file.

template<int size, bool big_endian>
void
Stub_table<size, big_endian>::do_write(Output_file* of)
{
  off_t offset = this->offset();
  const section_size_type oview_size =
    convert_to_section_size_type(this->data_size());
  unsigned char* const oview = of->get_output_view(offset, oview_size);

  // Write relocation stubs.
  for (typename Reloc_stub_map::const_iterator p = this->reloc_stubs_.begin();
      p != this->reloc_stubs_.end(); ++p)
    {
      The_reloc_stub* stub = p->second;
      AArch64_address address = this->address() + stub->offset();
      gold_assert(address ==
		  align_address(address, The_reloc_stub::STUB_ADDR_ALIGN));
      stub->write(oview + stub->offset(), stub->stub_size());
    }

  of->write_output_view(this->offset(), oview_size, oview);
}


// AArch64_relobj class.

template<int size, bool big_endian>
class AArch64_relobj : public Sized_relobj_file<size, big_endian>
{
 public:
  typedef AArch64_relobj<size, big_endian> This;
  typedef Target_aarch64<size, big_endian> The_target_aarch64;
  typedef AArch64_input_section<size, big_endian> The_aarch64_input_section;
  typedef typename elfcpp::Elf_types<size>::Elf_Addr AArch64_address;
  typedef Stub_table<size, big_endian> The_stub_table;
  typedef std::vector<The_stub_table*> Stub_table_list;
  static const AArch64_address invalid_address =
      static_cast<AArch64_address>(-1);

  AArch64_relobj(const std::string& name, Input_file* input_file, off_t offset,
		 const typename elfcpp::Ehdr<size, big_endian>& ehdr)
    : Sized_relobj_file<size, big_endian>(name, input_file, offset, ehdr),
      stub_tables_()
  { }

  ~AArch64_relobj()
  { }

  // Return the stub table of the SHNDX-th section if there is one.
  The_stub_table*
  stub_table(unsigned int shndx) const
  {
    gold_assert(shndx < this->stub_tables_.size());
    return this->stub_tables_[shndx];
  }

  // Set STUB_TABLE to be the stub_table of the SHNDX-th section.
  void
  set_stub_table(unsigned int shndx, The_stub_table* stub_table)
  {
    gold_assert(shndx < this->stub_tables_.size());
    this->stub_tables_[shndx] = stub_table;
  }

 // Scan all relocation sections for stub generation.
  void
  scan_sections_for_stubs(The_target_aarch64*, const Symbol_table*,
			  const Layout*);

  // Whether a section is a scannable text section.
  bool
  text_section_is_scannable(const elfcpp::Shdr<size, big_endian>&, unsigned int,
			    const Output_section*, const Symbol_table*);

  // Convert regular input section with index SHNDX to a relaxed section.
  void
  convert_input_section_to_relaxed_section(unsigned /* shndx */)
  {
    // The stubs have relocations and we need to process them after writing
    // out the stubs.  So relocation now must follow section write.
    this->set_relocs_must_follow_section_writes();
  }

 protected:
  // Post constructor setup.
  void
  do_setup()
  {
    // Call parent's setup method.
    Sized_relobj_file<size, big_endian>::do_setup();

    // Initialize look-up tables.
    this->stub_tables_.resize(this->shnum());
  }

  virtual void
  do_relocate_sections(
      const Symbol_table* symtab, const Layout* layout,
      const unsigned char* pshdrs, Output_file* of,
      typename Sized_relobj_file<size, big_endian>::Views* pviews);

 private:
  // Whether a section needs to be scanned for relocation stubs.
  bool
  section_needs_reloc_stub_scanning(const elfcpp::Shdr<size, big_endian>&,
				    const Relobj::Output_sections&,
				    const Symbol_table*, const unsigned char*);

  // List of stub tables.
  Stub_table_list stub_tables_;
};  // End of AArch64_relobj


// Relocate sections.

template<int size, bool big_endian>
void
AArch64_relobj<size, big_endian>::do_relocate_sections(
    const Symbol_table* symtab, const Layout* layout,
    const unsigned char* pshdrs, Output_file* of,
    typename Sized_relobj_file<size, big_endian>::Views* pviews)
{
  // Call parent to relocate sections.
  Sized_relobj_file<size, big_endian>::do_relocate_sections(symtab, layout,
							    pshdrs, of, pviews);

  // We do not generate stubs if doing a relocatable link.
  if (parameters->options().relocatable())
    return;

  Relocate_info<size, big_endian> relinfo;
  relinfo.symtab = symtab;
  relinfo.layout = layout;
  relinfo.object = this;

  // Relocate stub tables.
  unsigned int shnum = this->shnum();
  The_target_aarch64* target = The_target_aarch64::current_target();

  for (unsigned int i = 1; i < shnum; ++i)
    {
      The_aarch64_input_section* aarch64_input_section =
	  target->find_aarch64_input_section(this, i);
      if (aarch64_input_section != NULL
	  && aarch64_input_section->is_stub_table_owner()
	  && !aarch64_input_section->stub_table()->empty())
	{
	  Output_section* os = this->output_section(i);
	  gold_assert(os != NULL);

	  relinfo.reloc_shndx = elfcpp::SHN_UNDEF;
	  relinfo.reloc_shdr = NULL;
	  relinfo.data_shndx = i;
	  relinfo.data_shdr = pshdrs + i * elfcpp::Elf_sizes<size>::shdr_size;

	  typename Sized_relobj_file<size, big_endian>::View_size&
	      view_struct = (*pviews)[i];
	  gold_assert(view_struct.view != NULL);

	  The_stub_table* stub_table = aarch64_input_section->stub_table();
	  off_t offset = stub_table->address() - view_struct.address;
	  unsigned char* view = view_struct.view + offset;
	  AArch64_address address = stub_table->address();
	  section_size_type view_size = stub_table->data_size();
	  stub_table->relocate_stubs(&relinfo, target, os, view, address,
				     view_size);
	}
    }
}


// Determine if an input section is scannable for stub processing.  SHDR is
// the header of the section and SHNDX is the section index.  OS is the output
// section for the input section and SYMTAB is the global symbol table used to
// look up ICF information.

template<int size, bool big_endian>
bool
AArch64_relobj<size, big_endian>::text_section_is_scannable(
    const elfcpp::Shdr<size, big_endian>& text_shdr,
    unsigned int text_shndx,
    const Output_section* os,
    const Symbol_table* symtab)
{
  // Skip any empty sections, unallocated sections or sections whose
  // type are not SHT_PROGBITS.
  if (text_shdr.get_sh_size() == 0
      || (text_shdr.get_sh_flags() & elfcpp::SHF_ALLOC) == 0
      || text_shdr.get_sh_type() != elfcpp::SHT_PROGBITS)
    return false;

  // Skip any discarded or ICF'ed sections.
  if (os == NULL || symtab->is_section_folded(this, text_shndx))
    return false;

  // Skip exception frame.
  if (strcmp(os->name(), ".eh_frame") == 0)
    return false ;

  gold_assert(!this->is_output_section_offset_invalid(text_shndx) ||
	      os->find_relaxed_input_section(this, text_shndx) != NULL);

  return true;
}


// Determine if we want to scan the SHNDX-th section for relocation stubs.
// This is a helper for AArch64_relobj::scan_sections_for_stubs().

template<int size, bool big_endian>
bool
AArch64_relobj<size, big_endian>::section_needs_reloc_stub_scanning(
    const elfcpp::Shdr<size, big_endian>& shdr,
    const Relobj::Output_sections& out_sections,
    const Symbol_table* symtab,
    const unsigned char* pshdrs)
{
  unsigned int sh_type = shdr.get_sh_type();
  if (sh_type != elfcpp::SHT_RELA)
    return false;

  // Ignore empty section.
  off_t sh_size = shdr.get_sh_size();
  if (sh_size == 0)
    return false;

  // Ignore reloc section with unexpected symbol table.  The
  // error will be reported in the final link.
  if (this->adjust_shndx(shdr.get_sh_link()) != this->symtab_shndx())
    return false;

  gold_assert(sh_type == elfcpp::SHT_RELA);
  unsigned int reloc_size = elfcpp::Elf_sizes<size>::rela_size;

  // Ignore reloc section with unexpected entsize or uneven size.
  // The error will be reported in the final link.
  if (reloc_size != shdr.get_sh_entsize() || sh_size % reloc_size != 0)
    return false;

  // Ignore reloc section with bad info.  This error will be
  // reported in the final link.
  unsigned int text_shndx = this->adjust_shndx(shdr.get_sh_info());
  if (text_shndx >= this->shnum())
    return false;

  const unsigned int shdr_size = elfcpp::Elf_sizes<size>::shdr_size;
  const elfcpp::Shdr<size, big_endian> text_shdr(pshdrs +
						 text_shndx * shdr_size);
  return this->text_section_is_scannable(text_shdr, text_shndx,
					 out_sections[text_shndx], symtab);
}


// Scan relocations for stub generation.

template<int size, bool big_endian>
void
AArch64_relobj<size, big_endian>::scan_sections_for_stubs(
    The_target_aarch64* target,
    const Symbol_table* symtab,
    const Layout* layout)
{
  unsigned int shnum = this->shnum();
  const unsigned int shdr_size = elfcpp::Elf_sizes<size>::shdr_size;

  // Read the section headers.
  const unsigned char* pshdrs = this->get_view(this->elf_file()->shoff(),
					       shnum * shdr_size,
					       true, true);

  // To speed up processing, we set up hash tables for fast lookup of
  // input offsets to output addresses.
  this->initialize_input_to_output_maps();

  const Relobj::Output_sections& out_sections(this->output_sections());

  Relocate_info<size, big_endian> relinfo;
  relinfo.symtab = symtab;
  relinfo.layout = layout;
  relinfo.object = this;

  // Do relocation stubs scanning.
  const unsigned char* p = pshdrs + shdr_size;
  for (unsigned int i = 1; i < shnum; ++i, p += shdr_size)
    {
      const elfcpp::Shdr<size, big_endian> shdr(p);
      if (this->section_needs_reloc_stub_scanning(shdr, out_sections, symtab,
						  pshdrs))
	{
	  unsigned int index = this->adjust_shndx(shdr.get_sh_info());
	  AArch64_address output_offset =
	      this->get_output_section_offset(index);
	  AArch64_address output_address;
	  if (output_offset != invalid_address)
	    {
	      output_address = out_sections[index]->address() + output_offset;
	    }
	  else
	    {
	      // Currently this only happens for a relaxed section.
	      const Output_relaxed_input_section* poris =
		  out_sections[index]->find_relaxed_input_section(this, index);
	      gold_assert(poris != NULL);
	      output_address = poris->address();
	    }

	  // Get the relocations.
	  const unsigned char* prelocs = this->get_view(shdr.get_sh_offset(),
							shdr.get_sh_size(),
							true, false);

	  // Get the section contents.
	  section_size_type input_view_size = 0;
	  const unsigned char* input_view =
	      this->section_contents(index, &input_view_size, false);

	  relinfo.reloc_shndx = i;
	  relinfo.data_shndx = index;
	  unsigned int sh_type = shdr.get_sh_type();
	  unsigned int reloc_size;
	  gold_assert (sh_type == elfcpp::SHT_RELA);
	  reloc_size = elfcpp::Elf_sizes<size>::rela_size;

	  Output_section* os = out_sections[index];
	  target->scan_section_for_stubs(&relinfo, sh_type, prelocs,
					 shdr.get_sh_size() / reloc_size,
					 os,
					 output_offset == invalid_address,
					 input_view, output_address,
					 input_view_size);
	}
    }
}


// A class to wrap an ordinary input section containing executable code.

template<int size, bool big_endian>
class AArch64_input_section : public Output_relaxed_input_section
{
 public:
  typedef Stub_table<size, big_endian> The_stub_table;

  AArch64_input_section(Relobj* relobj, unsigned int shndx)
    : Output_relaxed_input_section(relobj, shndx, 1),
      stub_table_(NULL),
      original_contents_(NULL), original_size_(0),
      original_addralign_(1)
  { }

  ~AArch64_input_section()
  { delete[] this->original_contents_; }

  // Initialize.
  void
  init();

  // Set the stub_table.
  void
  set_stub_table(The_stub_table* st)
  { this->stub_table_ = st; }

  // Whether this is a stub table owner.
  bool
  is_stub_table_owner() const
  { return this->stub_table_ != NULL && this->stub_table_->owner() == this; }

  // Return the original size of the section.
  uint32_t
  original_size() const
  { return this->original_size_; }

  // Return the stub table.
  The_stub_table*
  stub_table()
  { return stub_table_; }

 protected:
  // Write out this input section.
  void
  do_write(Output_file*);

  // Return required alignment of this.
  uint64_t
  do_addralign() const
  {
    if (this->is_stub_table_owner())
      return std::max(this->stub_table_->addralign(),
		      static_cast<uint64_t>(this->original_addralign_));
    else
      return this->original_addralign_;
  }

  // Finalize data size.
  void
  set_final_data_size();

  // Reset address and file offset.
  void
  do_reset_address_and_file_offset();

  // Output offset.
  bool
  do_output_offset(const Relobj* object, unsigned int shndx,
		   section_offset_type offset,
		   section_offset_type* poutput) const
  {
    if ((object == this->relobj())
	&& (shndx == this->shndx())
	&& (offset >= 0)
	&& (offset <=
	    convert_types<section_offset_type, uint32_t>(this->original_size_)))
      {
	*poutput = offset;
	return true;
      }
    else
      return false;
  }

 private:
  // Copying is not allowed.
  AArch64_input_section(const AArch64_input_section&);
  AArch64_input_section& operator=(const AArch64_input_section&);

  // The relocation stubs.
  The_stub_table* stub_table_;
  // Original section contents.  We have to make a copy here since the file
  // containing the original section may not be locked when we need to access
  // the contents.
  unsigned char* original_contents_;
  // Section size of the original input section.
  uint32_t original_size_;
  // Address alignment of the original input section.
  uint32_t original_addralign_;
};  // End of AArch64_input_section


// Finalize data size.

template<int size, bool big_endian>
void
AArch64_input_section<size, big_endian>::set_final_data_size()
{
  off_t off = convert_types<off_t, uint64_t>(this->original_size_);

  if (this->is_stub_table_owner())
    {
      this->stub_table_->finalize_data_size();
      off = align_address(off, this->stub_table_->addralign());
      off += this->stub_table_->data_size();
    }
  this->set_data_size(off);
}


// Reset address and file offset.

template<int size, bool big_endian>
void
AArch64_input_section<size, big_endian>::do_reset_address_and_file_offset()
{
  // Size of the original input section contents.
  off_t off = convert_types<off_t, uint64_t>(this->original_size_);

  // If this is a stub table owner, account for the stub table size.
  if (this->is_stub_table_owner())
    {
      The_stub_table* stub_table = this->stub_table_;

      // Reset the stub table's address and file offset.  The
      // current data size for child will be updated after that.
      stub_table_->reset_address_and_file_offset();
      off = align_address(off, stub_table_->addralign());
      off += stub_table->current_data_size();
    }

  this->set_current_data_size(off);
}


// Initialize an Arm_input_section.

template<int size, bool big_endian>
void
AArch64_input_section<size, big_endian>::init()
{
  Relobj* relobj = this->relobj();
  unsigned int shndx = this->shndx();

  // We have to cache original size, alignment and contents to avoid locking
  // the original file.
  this->original_addralign_ =
      convert_types<uint32_t, uint64_t>(relobj->section_addralign(shndx));

  // This is not efficient but we expect only a small number of relaxed
  // input sections for stubs.
  section_size_type section_size;
  const unsigned char* section_contents =
      relobj->section_contents(shndx, &section_size, false);
  this->original_size_ =
      convert_types<uint32_t, uint64_t>(relobj->section_size(shndx));

  gold_assert(this->original_contents_ == NULL);
  this->original_contents_ = new unsigned char[section_size];
  memcpy(this->original_contents_, section_contents, section_size);

  // We want to make this look like the original input section after
  // output sections are finalized.
  Output_section* os = relobj->output_section(shndx);
  off_t offset = relobj->output_section_offset(shndx);
  gold_assert(os != NULL && !relobj->is_output_section_offset_invalid(shndx));
  this->set_address(os->address() + offset);
  this->set_file_offset(os->offset() + offset);
  this->set_current_data_size(this->original_size_);
  this->finalize_data_size();
}


// Write data to output file.

template<int size, bool big_endian>
void
AArch64_input_section<size, big_endian>::do_write(Output_file* of)
{
  // We have to write out the original section content.
  gold_assert(this->original_contents_ != NULL);
  of->write(this->offset(), this->original_contents_,
	    this->original_size_);

  // If this owns a stub table and it is not empty, write it.
  if (this->is_stub_table_owner() && !this->stub_table_->empty())
    this->stub_table_->write(of);
}


// Arm output section class.  This is defined mainly to add a number of stub
// generation methods.

template<int size, bool big_endian>
class AArch64_output_section : public Output_section
{
 public:
  typedef Target_aarch64<size, big_endian> The_target_aarch64;
  typedef AArch64_relobj<size, big_endian> The_aarch64_relobj;
  typedef Stub_table<size, big_endian> The_stub_table;
  typedef AArch64_input_section<size, big_endian> The_aarch64_input_section;

 public:
  AArch64_output_section(const char* name, elfcpp::Elf_Word type,
			 elfcpp::Elf_Xword flags)
    : Output_section(name, type, flags)
  { }

  ~AArch64_output_section() {}

  // Group input sections for stub generation.
  void
  group_sections(section_size_type, bool, Target_aarch64<size, big_endian>*,
		 const Task*);

 private:
  typedef Output_section::Input_section Input_section;
  typedef Output_section::Input_section_list Input_section_list;

  // Create a stub group.
  void
  create_stub_group(Input_section_list::const_iterator,
		    Input_section_list::const_iterator,
		    Input_section_list::const_iterator,
		    The_target_aarch64*,
		    std::vector<Output_relaxed_input_section*>&,
		    const Task*);
};  // End of AArch64_output_section


// Create a stub group for input sections from FIRST to LAST. OWNER points to
// the input section that will be the owner of the stub table.

template<int size, bool big_endian> void
AArch64_output_section<size, big_endian>::create_stub_group(
    Input_section_list::const_iterator first,
    Input_section_list::const_iterator last,
    Input_section_list::const_iterator owner,
    The_target_aarch64* target,
    std::vector<Output_relaxed_input_section*>& new_relaxed_sections,
    const Task* task)
{
  // Currently we convert ordinary input sections into relaxed sections only
  // at this point.
  The_aarch64_input_section* input_section;
  if (owner->is_relaxed_input_section())
    gold_unreachable();
  else
    {
      gold_assert(owner->is_input_section());
      // Create a new relaxed input section.  We need to lock the original
      // file.
      Task_lock_obj<Object> tl(task, owner->relobj());
      input_section =
	  target->new_aarch64_input_section(owner->relobj(), owner->shndx());
      new_relaxed_sections.push_back(input_section);
    }

  // Create a stub table.
  The_stub_table* stub_table =
      target->new_stub_table(input_section);

  input_section->set_stub_table(stub_table);

  Input_section_list::const_iterator p = first;
  // Look for input sections or relaxed input sections in [first ... last].
  do
    {
      if (p->is_input_section() || p->is_relaxed_input_section())
	{
	  // The stub table information for input sections live
	  // in their objects.
	  The_aarch64_relobj* aarch64_relobj =
	      static_cast<The_aarch64_relobj*>(p->relobj());
	  aarch64_relobj->set_stub_table(p->shndx(), stub_table);
	}
    }
  while (p++ != last);
}


// Group input sections for stub generation. GROUP_SIZE is roughly the limit of
// stub groups. We grow a stub group by adding input section until the size is
// just below GROUP_SIZE. The last input section will be converted into a stub
// table owner. If STUB_ALWAYS_AFTER_BRANCH is false, we also add input sectiond
// after the stub table, effectively doubling the group size.
//
// This is similar to the group_sections() function in elf32-arm.c but is
// implemented differently.

template<int size, bool big_endian>
void AArch64_output_section<size, big_endian>::group_sections(
    section_size_type group_size,
    bool stubs_always_after_branch,
    Target_aarch64<size, big_endian>* target,
    const Task* task)
{
  typedef enum
  {
    NO_GROUP,
    FINDING_STUB_SECTION,
    HAS_STUB_SECTION
  } State;

  std::vector<Output_relaxed_input_section*> new_relaxed_sections;

  State state = NO_GROUP;
  section_size_type off = 0;
  section_size_type group_begin_offset = 0;
  section_size_type group_end_offset = 0;
  section_size_type stub_table_end_offset = 0;
  Input_section_list::const_iterator group_begin =
      this->input_sections().end();
  Input_section_list::const_iterator stub_table =
      this->input_sections().end();
  Input_section_list::const_iterator group_end = this->input_sections().end();
  for (Input_section_list::const_iterator p = this->input_sections().begin();
       p != this->input_sections().end();
       ++p)
    {
      section_size_type section_begin_offset =
	align_address(off, p->addralign());
      section_size_type section_end_offset =
	section_begin_offset + p->data_size();

      // Check to see if we should group the previously seen sections.
      switch (state)
	{
	case NO_GROUP:
	  break;

	case FINDING_STUB_SECTION:
	  // Adding this section makes the group larger than GROUP_SIZE.
	  if (section_end_offset - group_begin_offset >= group_size)
	    {
	      if (stubs_always_after_branch)
		{
		  gold_assert(group_end != this->input_sections().end());
		  this->create_stub_group(group_begin, group_end, group_end,
					  target, new_relaxed_sections,
					  task);
		  state = NO_GROUP;
		}
	      else
		{
		  // Input sections up to stub_group_size bytes after the stub
		  // table can be handled by it too.
		  state = HAS_STUB_SECTION;
		  stub_table = group_end;
		  stub_table_end_offset = group_end_offset;
		}
	    }
	    break;

	case HAS_STUB_SECTION:
	  // Adding this section makes the post stub-section group larger
	  // than GROUP_SIZE.
	  gold_unreachable();
	  // NOT SUPPORTED YET. For completeness only.
	  if (section_end_offset - stub_table_end_offset >= group_size)
	   {
	     gold_assert(group_end != this->input_sections().end());
	     this->create_stub_group(group_begin, group_end, stub_table,
				     target, new_relaxed_sections, task);
	     state = NO_GROUP;
	   }
	   break;

	  default:
	    gold_unreachable();
	}

      // If we see an input section and currently there is no group, start
      // a new one.  Skip any empty sections.  We look at the data size
      // instead of calling p->relobj()->section_size() to avoid locking.
      if ((p->is_input_section() || p->is_relaxed_input_section())
	  && (p->data_size() != 0))
	{
	  if (state == NO_GROUP)
	    {
	      state = FINDING_STUB_SECTION;
	      group_begin = p;
	      group_begin_offset = section_begin_offset;
	    }

	  // Keep track of the last input section seen.
	  group_end = p;
	  group_end_offset = section_end_offset;
	}

      off = section_end_offset;
    }

  // Create a stub group for any ungrouped sections.
  if (state == FINDING_STUB_SECTION || state == HAS_STUB_SECTION)
    {
      gold_assert(group_end != this->input_sections().end());
      this->create_stub_group(group_begin, group_end,
			      (state == FINDING_STUB_SECTION
			       ? group_end
			       : stub_table),
			      target, new_relaxed_sections, task);
    }

  if (!new_relaxed_sections.empty())
    this->convert_input_sections_to_relaxed_sections(new_relaxed_sections);

  // Update the section offsets
  for (size_t i = 0; i < new_relaxed_sections.size(); ++i)
    {
      The_aarch64_relobj* relobj = static_cast<The_aarch64_relobj*>(
	  new_relaxed_sections[i]->relobj());
      unsigned int shndx = new_relaxed_sections[i]->shndx();
      // Tell AArch64_relobj that this input section is converted.
      relobj->convert_input_section_to_relaxed_section(shndx);
    }
}  // End of AArch64_output_section::group_sections


AArch64_reloc_property_table* aarch64_reloc_property_table = NULL;


// The aarch64 target class.
// See the ABI at
// http://infocenter.arm.com/help/topic/com.arm.doc.ihi0056b/IHI0056B_aaelf64.pdf
template<int size, bool big_endian>
class Target_aarch64 : public Sized_target<size, big_endian>
{
 public:
  typedef Target_aarch64<size, big_endian> This;
  typedef Output_data_reloc<elfcpp::SHT_RELA, true, size, big_endian>
      Reloc_section;
  typedef Relocate_info<size, big_endian> The_relocate_info;
  typedef typename elfcpp::Elf_types<size>::Elf_Addr Address;
  typedef AArch64_relobj<size, big_endian> The_aarch64_relobj;
  typedef Reloc_stub<size, big_endian> The_reloc_stub;
  typedef typename The_reloc_stub::Stub_type The_reloc_stub_type;
  typedef typename Reloc_stub<size, big_endian>::Key The_reloc_stub_key;
  typedef Stub_table<size, big_endian> The_stub_table;
  typedef std::vector<The_stub_table*> Stub_table_list;
  typedef typename Stub_table_list::iterator Stub_table_iterator;
  typedef AArch64_input_section<size, big_endian> The_aarch64_input_section;
  typedef AArch64_output_section<size, big_endian> The_aarch64_output_section;
  typedef Unordered_map<Section_id,
			AArch64_input_section<size, big_endian>*,
			Section_id_hash> AArch64_input_section_map;
  const static int TCB_SIZE = size / 8 * 2;

  Target_aarch64(const Target::Target_info* info = &aarch64_info)
    : Sized_target<size, big_endian>(info),
      got_(NULL), plt_(NULL), got_plt_(NULL), got_irelative_(NULL),
      got_tlsdesc_(NULL), global_offset_table_(NULL), rela_dyn_(NULL),
      rela_irelative_(NULL), copy_relocs_(elfcpp::R_AARCH64_COPY),
      got_mod_index_offset_(-1U),
      tlsdesc_reloc_info_(), tls_base_symbol_defined_(false),
      stub_tables_(), aarch64_input_section_map_()
  { }

  // Scan the relocations to determine unreferenced sections for
  // garbage collection.
  void
  gc_process_relocs(Symbol_table* symtab,
		    Layout* layout,
		    Sized_relobj_file<size, big_endian>* object,
		    unsigned int data_shndx,
		    unsigned int sh_type,
		    const unsigned char* prelocs,
		    size_t reloc_count,
		    Output_section* output_section,
		    bool needs_special_offset_handling,
		    size_t local_symbol_count,
		    const unsigned char* plocal_symbols);

  // Scan the relocations to look for symbol adjustments.
  void
  scan_relocs(Symbol_table* symtab,
	      Layout* layout,
	      Sized_relobj_file<size, big_endian>* object,
	      unsigned int data_shndx,
	      unsigned int sh_type,
	      const unsigned char* prelocs,
	      size_t reloc_count,
	      Output_section* output_section,
	      bool needs_special_offset_handling,
	      size_t local_symbol_count,
	      const unsigned char* plocal_symbols);

  // Finalize the sections.
  void
  do_finalize_sections(Layout*, const Input_objects*, Symbol_table*);

  // Return the value to use for a dynamic which requires special
  // treatment.
  uint64_t
  do_dynsym_value(const Symbol*) const;

  // Relocate a section.
  void
  relocate_section(const Relocate_info<size, big_endian>*,
		   unsigned int sh_type,
		   const unsigned char* prelocs,
		   size_t reloc_count,
		   Output_section* output_section,
		   bool needs_special_offset_handling,
		   unsigned char* view,
		   typename elfcpp::Elf_types<size>::Elf_Addr view_address,
		   section_size_type view_size,
		   const Reloc_symbol_changes*);

  // Scan the relocs during a relocatable link.
  void
  scan_relocatable_relocs(Symbol_table* symtab,
			  Layout* layout,
			  Sized_relobj_file<size, big_endian>* object,
			  unsigned int data_shndx,
			  unsigned int sh_type,
			  const unsigned char* prelocs,
			  size_t reloc_count,
			  Output_section* output_section,
			  bool needs_special_offset_handling,
			  size_t local_symbol_count,
			  const unsigned char* plocal_symbols,
			  Relocatable_relocs*);

  // Relocate a section during a relocatable link.
  void
  relocate_relocs(
      const Relocate_info<size, big_endian>*,
      unsigned int sh_type,
      const unsigned char* prelocs,
      size_t reloc_count,
      Output_section* output_section,
      typename elfcpp::Elf_types<size>::Elf_Off offset_in_output_section,
      const Relocatable_relocs*,
      unsigned char* view,
      typename elfcpp::Elf_types<size>::Elf_Addr view_address,
      section_size_type view_size,
      unsigned char* reloc_view,
      section_size_type reloc_view_size);

  // Return the symbol index to use for a target specific relocation.
  // The only target specific relocation is R_AARCH64_TLSDESC for a
  // local symbol, which is an absolute reloc.
  unsigned int
  do_reloc_symbol_index(void*, unsigned int r_type) const
  {
    gold_assert(r_type == elfcpp::R_AARCH64_TLSDESC);
    return 0;
  }

  // Return the addend to use for a target specific relocation.
  typename elfcpp::Elf_types<size>::Elf_Addr
  do_reloc_addend(void* arg, unsigned int r_type,
		  typename elfcpp::Elf_types<size>::Elf_Addr addend) const;

  // Return the PLT section.
  uint64_t
  do_plt_address_for_global(const Symbol* gsym) const
  { return this->plt_section()->address_for_global(gsym); }

  uint64_t
  do_plt_address_for_local(const Relobj* relobj, unsigned int symndx) const
  { return this->plt_section()->address_for_local(relobj, symndx); }

  // This function should be defined in targets that can use relocation
  // types to determine (implemented in local_reloc_may_be_function_pointer
  // and global_reloc_may_be_function_pointer)
  // if a function's pointer is taken.  ICF uses this in safe mode to only
  // fold those functions whose pointer is defintely not taken.
  bool
  do_can_check_for_function_pointers() const
  { return true; }

  // Return the number of entries in the PLT.
  unsigned int
  plt_entry_count() const;

  //Return the offset of the first non-reserved PLT entry.
  unsigned int
  first_plt_entry_offset() const;

  // Return the size of each PLT entry.
  unsigned int
  plt_entry_size() const;

  // Create a stub table.
  The_stub_table*
  new_stub_table(The_aarch64_input_section*);

  // Create an aarch64 input section.
  The_aarch64_input_section*
  new_aarch64_input_section(Relobj*, unsigned int);

  // Find an aarch64 input section instance for a given OBJ and SHNDX.
  The_aarch64_input_section*
  find_aarch64_input_section(Relobj*, unsigned int) const;

  // Return the thread control block size.
  unsigned int
  tcb_size() const { return This::TCB_SIZE; }

  // Scan a section for stub generation.
  void
  scan_section_for_stubs(const Relocate_info<size, big_endian>*, unsigned int,
			 const unsigned char*, size_t, Output_section*,
			 bool, const unsigned char*,
			 Address,
			 section_size_type);

  // Scan a relocation section for stub.
  template<int sh_type>
  void
  scan_reloc_section_for_stubs(
      const The_relocate_info* relinfo,
      const unsigned char* prelocs,
      size_t reloc_count,
      Output_section* output_section,
      bool needs_special_offset_handling,
      const unsigned char* view,
      Address view_address,
      section_size_type);

  // Relocate a single stub.
  void
  relocate_stub(The_reloc_stub*, const Relocate_info<size, big_endian>*,
		Output_section*, unsigned char*, Address,
		section_size_type);

  // Get the default AArch64 target.
  static This*
  current_target()
  {
    gold_assert(parameters->target().machine_code() == elfcpp::EM_AARCH64
		&& parameters->target().get_size() == size
		&& parameters->target().is_big_endian() == big_endian);
    return static_cast<This*>(parameters->sized_target<size, big_endian>());
  }

 protected:
  void
  do_select_as_default_target()
  {
    gold_assert(aarch64_reloc_property_table == NULL);
    aarch64_reloc_property_table = new AArch64_reloc_property_table();
  }

  // Add a new reloc argument, returning the index in the vector.
  size_t
  add_tlsdesc_info(Sized_relobj_file<size, big_endian>* object,
		   unsigned int r_sym)
  {
    this->tlsdesc_reloc_info_.push_back(Tlsdesc_info(object, r_sym));
    return this->tlsdesc_reloc_info_.size() - 1;
  }

  virtual Output_data_plt_aarch64<size, big_endian>*
  do_make_data_plt(Layout* layout,
		   Output_data_got_aarch64<size, big_endian>* got,
		   Output_data_space* got_plt,
		   Output_data_space* got_irelative)
  {
    return new Output_data_plt_aarch64_standard<size, big_endian>(
      layout, got, got_plt, got_irelative);
  }


  // do_make_elf_object to override the same function in the base class.
  Object*
  do_make_elf_object(const std::string&, Input_file*, off_t,
		     const elfcpp::Ehdr<size, big_endian>&);

  Output_data_plt_aarch64<size, big_endian>*
  make_data_plt(Layout* layout,
		Output_data_got_aarch64<size, big_endian>* got,
		Output_data_space* got_plt,
		Output_data_space* got_irelative)
  {
    return this->do_make_data_plt(layout, got, got_plt, got_irelative);
  }

  // We only need to generate stubs, and hence perform relaxation if we are
  // not doing relocatable linking.
  virtual bool
  do_may_relax() const
  { return !parameters->options().relocatable(); }

  // Relaxation hook.  This is where we do stub generation.
  virtual bool
  do_relax(int, const Input_objects*, Symbol_table*, Layout*, const Task*);

  void
  group_sections(Layout* layout,
		 section_size_type group_size,
		 bool stubs_always_after_branch,
		 const Task* task);

  void
  scan_reloc_for_stub(const The_relocate_info*, unsigned int,
		      const Sized_symbol<size>*, unsigned int,
		      const Symbol_value<size>*,
		      typename elfcpp::Elf_types<size>::Elf_Swxword,
		      Address Elf_Addr);

  // Make an output section.
  Output_section*
  do_make_output_section(const char* name, elfcpp::Elf_Word type,
			 elfcpp::Elf_Xword flags)
  { return new The_aarch64_output_section(name, type, flags); }

 private:
  // The class which scans relocations.
  class Scan
  {
  public:
    Scan()
      : issued_non_pic_error_(false)
    { }

    inline void
    local(Symbol_table* symtab, Layout* layout, Target_aarch64* target,
	  Sized_relobj_file<size, big_endian>* object,
	  unsigned int data_shndx,
	  Output_section* output_section,
	  const elfcpp::Rela<size, big_endian>& reloc, unsigned int r_type,
	  const elfcpp::Sym<size, big_endian>& lsym,
	  bool is_discarded);

    inline void
    global(Symbol_table* symtab, Layout* layout, Target_aarch64* target,
	   Sized_relobj_file<size, big_endian>* object,
	   unsigned int data_shndx,
	   Output_section* output_section,
	   const elfcpp::Rela<size, big_endian>& reloc, unsigned int r_type,
	   Symbol* gsym);

    inline bool
    local_reloc_may_be_function_pointer(Symbol_table* , Layout* ,
					Target_aarch64<size, big_endian>* ,
					Sized_relobj_file<size, big_endian>* ,
					unsigned int ,
					Output_section* ,
					const elfcpp::Rela<size, big_endian>& ,
					unsigned int r_type,
					const elfcpp::Sym<size, big_endian>&);

    inline bool
    global_reloc_may_be_function_pointer(Symbol_table* , Layout* ,
					 Target_aarch64<size, big_endian>* ,
					 Sized_relobj_file<size, big_endian>* ,
					 unsigned int ,
					 Output_section* ,
					 const elfcpp::Rela<size, big_endian>& ,
					 unsigned int r_type,
					 Symbol* gsym);

  private:
    static void
    unsupported_reloc_local(Sized_relobj_file<size, big_endian>*,
			    unsigned int r_type);

    static void
    unsupported_reloc_global(Sized_relobj_file<size, big_endian>*,
			     unsigned int r_type, Symbol*);

    inline bool
    possible_function_pointer_reloc(unsigned int r_type);

    void
    check_non_pic(Relobj*, unsigned int r_type);

    bool
    reloc_needs_plt_for_ifunc(Sized_relobj_file<size, big_endian>*,
			      unsigned int r_type);

    // Whether we have issued an error about a non-PIC compilation.
    bool issued_non_pic_error_;
  };

  // The class which implements relocation.
  class Relocate
  {
   public:
    Relocate()
      : skip_call_tls_get_addr_(false)
    { }

    ~Relocate()
    { }

    // Do a relocation.  Return false if the caller should not issue
    // any warnings about this relocation.
    inline bool
    relocate(const Relocate_info<size, big_endian>*, Target_aarch64*,
	     Output_section*,
	     size_t relnum, const elfcpp::Rela<size, big_endian>&,
	     unsigned int r_type, const Sized_symbol<size>*,
	     const Symbol_value<size>*,
	     unsigned char*, typename elfcpp::Elf_types<size>::Elf_Addr,
	     section_size_type);

  private:
    inline typename AArch64_relocate_functions<size, big_endian>::Status
    relocate_tls(const Relocate_info<size, big_endian>*,
		 Target_aarch64<size, big_endian>*,
		 size_t,
		 const elfcpp::Rela<size, big_endian>&,
		 unsigned int r_type, const Sized_symbol<size>*,
		 const Symbol_value<size>*,
		 unsigned char*,
		 typename elfcpp::Elf_types<size>::Elf_Addr);

    inline typename AArch64_relocate_functions<size, big_endian>::Status
    tls_gd_to_le(
		 const Relocate_info<size, big_endian>*,
		 Target_aarch64<size, big_endian>*,
		 const elfcpp::Rela<size, big_endian>&,
		 unsigned int,
		 unsigned char*,
		 const Symbol_value<size>*);

    inline typename AArch64_relocate_functions<size, big_endian>::Status
    tls_ld_to_le(
		 const Relocate_info<size, big_endian>*,
		 Target_aarch64<size, big_endian>*,
		 const elfcpp::Rela<size, big_endian>&,
		 unsigned int,
		 unsigned char*,
		 const Symbol_value<size>*);

    inline typename AArch64_relocate_functions<size, big_endian>::Status
    tls_ie_to_le(
		 const Relocate_info<size, big_endian>*,
		 Target_aarch64<size, big_endian>*,
		 const elfcpp::Rela<size, big_endian>&,
		 unsigned int,
		 unsigned char*,
		 const Symbol_value<size>*);

    inline typename AArch64_relocate_functions<size, big_endian>::Status
    tls_desc_gd_to_le(
		 const Relocate_info<size, big_endian>*,
		 Target_aarch64<size, big_endian>*,
		 const elfcpp::Rela<size, big_endian>&,
		 unsigned int,
		 unsigned char*,
		 const Symbol_value<size>*);

    inline typename AArch64_relocate_functions<size, big_endian>::Status
    tls_desc_gd_to_ie(
		 const Relocate_info<size, big_endian>*,
		 Target_aarch64<size, big_endian>*,
		 const elfcpp::Rela<size, big_endian>&,
		 unsigned int,
		 unsigned char*,
		 const Symbol_value<size>*,
		 typename elfcpp::Elf_types<size>::Elf_Addr,
		 typename elfcpp::Elf_types<size>::Elf_Addr);

    bool skip_call_tls_get_addr_;

  };  // End of class Relocate

  // A class which returns the size required for a relocation type,
  // used while scanning relocs during a relocatable link.
  class Relocatable_size_for_reloc
  {
   public:
    unsigned int
    get_size_for_reloc(unsigned int, Relobj*);
  };

  // Adjust TLS relocation type based on the options and whether this
  // is a local symbol.
  static tls::Tls_optimization
  optimize_tls_reloc(bool is_final, int r_type);

  // Get the GOT section, creating it if necessary.
  Output_data_got_aarch64<size, big_endian>*
  got_section(Symbol_table*, Layout*);

  // Get the GOT PLT section.
  Output_data_space*
  got_plt_section() const
  {
    gold_assert(this->got_plt_ != NULL);
    return this->got_plt_;
  }

  // Get the GOT section for TLSDESC entries.
  Output_data_got<size, big_endian>*
  got_tlsdesc_section() const
  {
    gold_assert(this->got_tlsdesc_ != NULL);
    return this->got_tlsdesc_;
  }

  // Create the PLT section.
  void
  make_plt_section(Symbol_table* symtab, Layout* layout);

  // Create a PLT entry for a global symbol.
  void
  make_plt_entry(Symbol_table*, Layout*, Symbol*);

  // Create a PLT entry for a local STT_GNU_IFUNC symbol.
  void
  make_local_ifunc_plt_entry(Symbol_table*, Layout*,
			     Sized_relobj_file<size, big_endian>* relobj,
			     unsigned int local_sym_index);

  // Define the _TLS_MODULE_BASE_ symbol in the TLS segment.
  void
  define_tls_base_symbol(Symbol_table*, Layout*);

  // Create the reserved PLT and GOT entries for the TLS descriptor resolver.
  void
  reserve_tlsdesc_entries(Symbol_table* symtab, Layout* layout);

  // Create a GOT entry for the TLS module index.
  unsigned int
  got_mod_index_entry(Symbol_table* symtab, Layout* layout,
		      Sized_relobj_file<size, big_endian>* object);

  // Get the PLT section.
  Output_data_plt_aarch64<size, big_endian>*
  plt_section() const
  {
    gold_assert(this->plt_ != NULL);
    return this->plt_;
  }

  // Get the dynamic reloc section, creating it if necessary.
  Reloc_section*
  rela_dyn_section(Layout*);

  // Get the section to use for TLSDESC relocations.
  Reloc_section*
  rela_tlsdesc_section(Layout*) const;

  // Get the section to use for IRELATIVE relocations.
  Reloc_section*
  rela_irelative_section(Layout*);

  // Add a potential copy relocation.
  void
  copy_reloc(Symbol_table* symtab, Layout* layout,
	     Sized_relobj_file<size, big_endian>* object,
	     unsigned int shndx, Output_section* output_section,
	     Symbol* sym, const elfcpp::Rela<size, big_endian>& reloc)
  {
    this->copy_relocs_.copy_reloc(symtab, layout,
				  symtab->get_sized_symbol<size>(sym),
				  object, shndx, output_section,
				  reloc, this->rela_dyn_section(layout));
  }

  // Information about this specific target which we pass to the
  // general Target structure.
  static const Target::Target_info aarch64_info;

  // The types of GOT entries needed for this platform.
  // These values are exposed to the ABI in an incremental link.
  // Do not renumber existing values without changing the version
  // number of the .gnu_incremental_inputs section.
  enum Got_type
  {
    GOT_TYPE_STANDARD = 0,      // GOT entry for a regular symbol
    GOT_TYPE_TLS_OFFSET = 1,    // GOT entry for TLS offset
    GOT_TYPE_TLS_PAIR = 2,      // GOT entry for TLS module/offset pair
    GOT_TYPE_TLS_DESC = 3       // GOT entry for TLS_DESC pair
  };

  // This type is used as the argument to the target specific
  // relocation routines.  The only target specific reloc is
  // R_AARCh64_TLSDESC against a local symbol.
  struct Tlsdesc_info
  {
    Tlsdesc_info(Sized_relobj_file<size, big_endian>* a_object,
		 unsigned int a_r_sym)
      : object(a_object), r_sym(a_r_sym)
    { }

    // The object in which the local symbol is defined.
    Sized_relobj_file<size, big_endian>* object;
    // The local symbol index in the object.
    unsigned int r_sym;
  };

  // The GOT section.
  Output_data_got_aarch64<size, big_endian>* got_;
  // The PLT section.
  Output_data_plt_aarch64<size, big_endian>* plt_;
  // The GOT PLT section.
  Output_data_space* got_plt_;
  // The GOT section for IRELATIVE relocations.
  Output_data_space* got_irelative_;
  // The GOT section for TLSDESC relocations.
  Output_data_got<size, big_endian>* got_tlsdesc_;
  // The _GLOBAL_OFFSET_TABLE_ symbol.
  Symbol* global_offset_table_;
  // The dynamic reloc section.
  Reloc_section* rela_dyn_;
  // The section to use for IRELATIVE relocs.
  Reloc_section* rela_irelative_;
  // Relocs saved to avoid a COPY reloc.
  Copy_relocs<elfcpp::SHT_RELA, size, big_endian> copy_relocs_;
  // Offset of the GOT entry for the TLS module index.
  unsigned int got_mod_index_offset_;
  // We handle R_AARCH64_TLSDESC against a local symbol as a target
  // specific relocation. Here we store the object and local symbol
  // index for the relocation.
  std::vector<Tlsdesc_info> tlsdesc_reloc_info_;
  // True if the _TLS_MODULE_BASE_ symbol has been defined.
  bool tls_base_symbol_defined_;
  // List of stub_tables
  Stub_table_list stub_tables_;
  AArch64_input_section_map aarch64_input_section_map_;
};  // End of Target_aarch64


template<>
const Target::Target_info Target_aarch64<64, false>::aarch64_info =
{
  64,			// size
  false,		// is_big_endian
  elfcpp::EM_AARCH64,	// machine_code
  false,		// has_make_symbol
  false,		// has_resolve
  false,		// has_code_fill
  true,			// is_default_stack_executable
  true,			// can_icf_inline_merge_sections
  '\0',			// wrap_char
  "/lib/ld.so.1",	// program interpreter
  0x400000,		// default_text_segment_address
  0x1000,		// abi_pagesize (overridable by -z max-page-size)
  0x1000,		// common_pagesize (overridable by -z common-page-size)
  false,                // isolate_execinstr
  0,                    // rosegment_gap
  elfcpp::SHN_UNDEF,	// small_common_shndx
  elfcpp::SHN_UNDEF,	// large_common_shndx
  0,			// small_common_section_flags
  0,			// large_common_section_flags
  NULL,			// attributes_section
  NULL,			// attributes_vendor
  "_start"		// entry_symbol_name
};

template<>
const Target::Target_info Target_aarch64<32, false>::aarch64_info =
{
  32,			// size
  false,		// is_big_endian
  elfcpp::EM_AARCH64,	// machine_code
  false,		// has_make_symbol
  false,		// has_resolve
  false,		// has_code_fill
  true,			// is_default_stack_executable
  false,		// can_icf_inline_merge_sections
  '\0',			// wrap_char
  "/lib/ld.so.1",	// program interpreter
  0x400000,		// default_text_segment_address
  0x1000,		// abi_pagesize (overridable by -z max-page-size)
  0x1000,		// common_pagesize (overridable by -z common-page-size)
  false,                // isolate_execinstr
  0,                    // rosegment_gap
  elfcpp::SHN_UNDEF,	// small_common_shndx
  elfcpp::SHN_UNDEF,	// large_common_shndx
  0,			// small_common_section_flags
  0,			// large_common_section_flags
  NULL,			// attributes_section
  NULL,			// attributes_vendor
  "_start"		// entry_symbol_name
};

template<>
const Target::Target_info Target_aarch64<64, true>::aarch64_info =
{
  64,			// size
  true,			// is_big_endian
  elfcpp::EM_AARCH64,	// machine_code
  false,		// has_make_symbol
  false,		// has_resolve
  false,		// has_code_fill
  true,			// is_default_stack_executable
  true,			// can_icf_inline_merge_sections
  '\0',			// wrap_char
  "/lib/ld.so.1",	// program interpreter
  0x400000,		// default_text_segment_address
  0x1000,		// abi_pagesize (overridable by -z max-page-size)
  0x1000,		// common_pagesize (overridable by -z common-page-size)
  false,                // isolate_execinstr
  0,                    // rosegment_gap
  elfcpp::SHN_UNDEF,	// small_common_shndx
  elfcpp::SHN_UNDEF,	// large_common_shndx
  0,			// small_common_section_flags
  0,			// large_common_section_flags
  NULL,			// attributes_section
  NULL,			// attributes_vendor
  "_start"		// entry_symbol_name
};

template<>
const Target::Target_info Target_aarch64<32, true>::aarch64_info =
{
  32,			// size
  true,			// is_big_endian
  elfcpp::EM_AARCH64,	// machine_code
  false,		// has_make_symbol
  false,		// has_resolve
  false,		// has_code_fill
  true,			// is_default_stack_executable
  false,		// can_icf_inline_merge_sections
  '\0',			// wrap_char
  "/lib/ld.so.1",	// program interpreter
  0x400000,		// default_text_segment_address
  0x1000,		// abi_pagesize (overridable by -z max-page-size)
  0x1000,		// common_pagesize (overridable by -z common-page-size)
  false,                // isolate_execinstr
  0,                    // rosegment_gap
  elfcpp::SHN_UNDEF,	// small_common_shndx
  elfcpp::SHN_UNDEF,	// large_common_shndx
  0,			// small_common_section_flags
  0,			// large_common_section_flags
  NULL,			// attributes_section
  NULL,			// attributes_vendor
  "_start"		// entry_symbol_name
};

// Get the GOT section, creating it if necessary.

template<int size, bool big_endian>
Output_data_got_aarch64<size, big_endian>*
Target_aarch64<size, big_endian>::got_section(Symbol_table* symtab,
					      Layout* layout)
{
  if (this->got_ == NULL)
    {
      gold_assert(symtab != NULL && layout != NULL);

      // When using -z now, we can treat .got.plt as a relro section.
      // Without -z now, it is modified after program startup by lazy
      // PLT relocations.
      bool is_got_plt_relro = parameters->options().now();
      Output_section_order got_order = (is_got_plt_relro
					? ORDER_RELRO
					: ORDER_RELRO_LAST);
      Output_section_order got_plt_order = (is_got_plt_relro
					    ? ORDER_RELRO
					    : ORDER_NON_RELRO_FIRST);

      // Layout of .got and .got.plt sections.
      // .got[0] &_DYNAMIC                          <-_GLOBAL_OFFSET_TABLE_
      // ...
      // .gotplt[0] reserved for ld.so (&linkmap)   <--DT_PLTGOT
      // .gotplt[1] reserved for ld.so (resolver)
      // .gotplt[2] reserved

      // Generate .got section.
      this->got_ = new Output_data_got_aarch64<size, big_endian>(symtab,
								 layout);
      layout->add_output_section_data(".got", elfcpp::SHT_PROGBITS,
				      (elfcpp::SHF_ALLOC | elfcpp::SHF_WRITE),
				      this->got_, got_order, true);
      // The first word of GOT is reserved for the address of .dynamic.
      // We put 0 here now. The value will be replaced later in
      // Output_data_got_aarch64::do_write.
      this->got_->add_constant(0);

      // Define _GLOBAL_OFFSET_TABLE_ at the start of the PLT.
      // _GLOBAL_OFFSET_TABLE_ value points to the start of the .got section,
      // even if there is a .got.plt section.
      this->global_offset_table_ =
	symtab->define_in_output_data("_GLOBAL_OFFSET_TABLE_", NULL,
				      Symbol_table::PREDEFINED,
				      this->got_,
				      0, 0, elfcpp::STT_OBJECT,
				      elfcpp::STB_LOCAL,
				      elfcpp::STV_HIDDEN, 0,
				      false, false);

      // Generate .got.plt section.
      this->got_plt_ = new Output_data_space(size / 8, "** GOT PLT");
      layout->add_output_section_data(".got.plt", elfcpp::SHT_PROGBITS,
				      (elfcpp::SHF_ALLOC
				       | elfcpp::SHF_WRITE),
				      this->got_plt_, got_plt_order,
				      is_got_plt_relro);

      // The first three entries are reserved.
      this->got_plt_->set_current_data_size(
	AARCH64_GOTPLT_RESERVE_COUNT * (size / 8));

      // If there are any IRELATIVE relocations, they get GOT entries
      // in .got.plt after the jump slot entries.
      this->got_irelative_ = new Output_data_space(size / 8,
						   "** GOT IRELATIVE PLT");
      layout->add_output_section_data(".got.plt", elfcpp::SHT_PROGBITS,
				      (elfcpp::SHF_ALLOC
				       | elfcpp::SHF_WRITE),
				      this->got_irelative_,
				      got_plt_order,
				      is_got_plt_relro);

      // If there are any TLSDESC relocations, they get GOT entries in
      // .got.plt after the jump slot and IRELATIVE entries.
      this->got_tlsdesc_ = new Output_data_got<size, big_endian>();
      layout->add_output_section_data(".got.plt", elfcpp::SHT_PROGBITS,
				      (elfcpp::SHF_ALLOC
				       | elfcpp::SHF_WRITE),
				      this->got_tlsdesc_,
				      got_plt_order,
				      is_got_plt_relro);

      if (!is_got_plt_relro)
	{
	  // Those bytes can go into the relro segment.
	  layout->increase_relro(
	    AARCH64_GOTPLT_RESERVE_COUNT * (size / 8));
	}

    }
  return this->got_;
}

// Get the dynamic reloc section, creating it if necessary.

template<int size, bool big_endian>
typename Target_aarch64<size, big_endian>::Reloc_section*
Target_aarch64<size, big_endian>::rela_dyn_section(Layout* layout)
{
  if (this->rela_dyn_ == NULL)
    {
      gold_assert(layout != NULL);
      this->rela_dyn_ = new Reloc_section(parameters->options().combreloc());
      layout->add_output_section_data(".rela.dyn", elfcpp::SHT_RELA,
				      elfcpp::SHF_ALLOC, this->rela_dyn_,
				      ORDER_DYNAMIC_RELOCS, false);
    }
  return this->rela_dyn_;
}

// Get the section to use for IRELATIVE relocs, creating it if
// necessary.  These go in .rela.dyn, but only after all other dynamic
// relocations.  They need to follow the other dynamic relocations so
// that they can refer to global variables initialized by those
// relocs.

template<int size, bool big_endian>
typename Target_aarch64<size, big_endian>::Reloc_section*
Target_aarch64<size, big_endian>::rela_irelative_section(Layout* layout)
{
  if (this->rela_irelative_ == NULL)
    {
      // Make sure we have already created the dynamic reloc section.
      this->rela_dyn_section(layout);
      this->rela_irelative_ = new Reloc_section(false);
      layout->add_output_section_data(".rela.dyn", elfcpp::SHT_RELA,
				      elfcpp::SHF_ALLOC, this->rela_irelative_,
				      ORDER_DYNAMIC_RELOCS, false);
      gold_assert(this->rela_dyn_->output_section()
		  == this->rela_irelative_->output_section());
    }
  return this->rela_irelative_;
}


// do_make_elf_object to override the same function in the base class.  We need
// to use a target-specific sub-class of Sized_relobj_file<size, big_endian> to
// store backend specific information. Hence we need to have our own ELF object
// creation.

template<int size, bool big_endian>
Object*
Target_aarch64<size, big_endian>::do_make_elf_object(
    const std::string& name,
    Input_file* input_file,
    off_t offset, const elfcpp::Ehdr<size, big_endian>& ehdr)
{
  int et = ehdr.get_e_type();
  // ET_EXEC files are valid input for --just-symbols/-R,
  // and we treat them as relocatable objects.
  if (et == elfcpp::ET_EXEC && input_file->just_symbols())
    return Sized_target<size, big_endian>::do_make_elf_object(
	name, input_file, offset, ehdr);
  else if (et == elfcpp::ET_REL)
    {
      AArch64_relobj<size, big_endian>* obj =
	new AArch64_relobj<size, big_endian>(name, input_file, offset, ehdr);
      obj->setup();
      return obj;
    }
  else if (et == elfcpp::ET_DYN)
    {
      // Keep base implementation.
      Sized_dynobj<size, big_endian>* obj =
	  new Sized_dynobj<size, big_endian>(name, input_file, offset, ehdr);
      obj->setup();
      return obj;
    }
  else
    {
      gold_error(_("%s: unsupported ELF file type %d"),
		 name.c_str(), et);
      return NULL;
    }
}


// Scan a relocation for stub generation.

template<int size, bool big_endian>
void
Target_aarch64<size, big_endian>::scan_reloc_for_stub(
    const Relocate_info<size, big_endian>* relinfo,
    unsigned int r_type,
    const Sized_symbol<size>* gsym,
    unsigned int r_sym,
    const Symbol_value<size>* psymval,
    typename elfcpp::Elf_types<size>::Elf_Swxword addend,
    Address address)
{
  const AArch64_relobj<size, big_endian>* aarch64_relobj =
      static_cast<AArch64_relobj<size, big_endian>*>(relinfo->object);

  Symbol_value<size> symval;
  if (gsym != NULL)
    {
      const AArch64_reloc_property* arp = aarch64_reloc_property_table->
	get_reloc_property(r_type);
      if (gsym->use_plt_offset(arp->reference_flags()))
	{
	  // This uses a PLT, change the symbol value.
	  symval.set_output_value(this->plt_section()->address()
				  + gsym->plt_offset());
	  psymval = &symval;
	}
      else if (gsym->is_undefined())
	// There is no need to generate a stub symbol is undefined.
	return;
    }

  // Get the symbol value.
  typename Symbol_value<size>::Value value = psymval->value(aarch64_relobj, 0);

  // Owing to pipelining, the PC relative branches below actually skip
  // two instructions when the branch offset is 0.
  Address destination = static_cast<Address>(-1);
  switch (r_type)
    {
    case elfcpp::R_AARCH64_CALL26:
    case elfcpp::R_AARCH64_JUMP26:
      destination = value + addend;
      break;
    default:
      gold_unreachable();
    }

  typename The_reloc_stub::Stub_type stub_type = The_reloc_stub::
      stub_type_for_reloc(r_type, address, destination);
  if (stub_type == The_reloc_stub::ST_NONE)
    return ;

  The_stub_table* stub_table = aarch64_relobj->stub_table(relinfo->data_shndx);
  gold_assert(stub_table != NULL);

  The_reloc_stub_key key(stub_type, gsym, aarch64_relobj, r_sym, addend);
  The_reloc_stub* stub = stub_table->find_reloc_stub(key);
  if (stub == NULL)
    {
      stub = new The_reloc_stub(stub_type);
      stub_table->add_reloc_stub(stub, key);
    }
  stub->set_destination_address(destination);
}  // End of Target_aarch64::scan_reloc_for_stub


// This function scans a relocation section for stub generation.
// The template parameter Relocate must be a class type which provides
// a single function, relocate(), which implements the machine
// specific part of a relocation.

// BIG_ENDIAN is the endianness of the data.  SH_TYPE is the section type:
// SHT_REL or SHT_RELA.

// PRELOCS points to the relocation data.  RELOC_COUNT is the number
// of relocs.  OUTPUT_SECTION is the output section.
// NEEDS_SPECIAL_OFFSET_HANDLING is true if input offsets need to be
// mapped to output offsets.

// VIEW is the section data, VIEW_ADDRESS is its memory address, and
// VIEW_SIZE is the size.  These refer to the input section, unless
// NEEDS_SPECIAL_OFFSET_HANDLING is true, in which case they refer to
// the output section.

template<int size, bool big_endian>
template<int sh_type>
void inline
Target_aarch64<size, big_endian>::scan_reloc_section_for_stubs(
    const Relocate_info<size, big_endian>* relinfo,
    const unsigned char* prelocs,
    size_t reloc_count,
    Output_section* /*output_section*/,
    bool /*needs_special_offset_handling*/,
    const unsigned char* /*view*/,
    Address view_address,
    section_size_type)
{
  typedef typename Reloc_types<sh_type,size,big_endian>::Reloc Reltype;

  const int reloc_size =
      Reloc_types<sh_type,size,big_endian>::reloc_size;
  AArch64_relobj<size, big_endian>* object =
      static_cast<AArch64_relobj<size, big_endian>*>(relinfo->object);
  unsigned int local_count = object->local_symbol_count();

  gold::Default_comdat_behavior default_comdat_behavior;
  Comdat_behavior comdat_behavior = CB_UNDETERMINED;

  for (size_t i = 0; i < reloc_count; ++i, prelocs += reloc_size)
    {
      Reltype reloc(prelocs);
      typename elfcpp::Elf_types<size>::Elf_WXword r_info = reloc.get_r_info();
      unsigned int r_sym = elfcpp::elf_r_sym<size>(r_info);
      unsigned int r_type = elfcpp::elf_r_type<size>(r_info);
      if (r_type != elfcpp::R_AARCH64_CALL26
	  && r_type != elfcpp::R_AARCH64_JUMP26)
	continue;

      section_offset_type offset =
	  convert_to_section_size_type(reloc.get_r_offset());

      // Get the addend.
      typename elfcpp::Elf_types<size>::Elf_Swxword addend =
	  reloc.get_r_addend();

      const Sized_symbol<size>* sym;
      Symbol_value<size> symval;
      const Symbol_value<size> *psymval;
      bool is_defined_in_discarded_section;
      unsigned int shndx;
      if (r_sym < local_count)
	{
	  sym = NULL;
	  psymval = object->local_symbol(r_sym);

	  // If the local symbol belongs to a section we are discarding,
	  // and that section is a debug section, try to find the
	  // corresponding kept section and map this symbol to its
	  // counterpart in the kept section.  The symbol must not
	  // correspond to a section we are folding.
	  bool is_ordinary;
	  shndx = psymval->input_shndx(&is_ordinary);
	  is_defined_in_discarded_section =
	    (is_ordinary
	     && shndx != elfcpp::SHN_UNDEF
	     && !object->is_section_included(shndx)
	     && !relinfo->symtab->is_section_folded(object, shndx));

	  // We need to compute the would-be final value of this local
	  // symbol.
	  if (!is_defined_in_discarded_section)
	    {
	      typedef Sized_relobj_file<size, big_endian> ObjType;
	      typename ObjType::Compute_final_local_value_status status =
		object->compute_final_local_value(r_sym, psymval, &symval,
						  relinfo->symtab);
	      if (status == ObjType::CFLV_OK)
		{
		  // Currently we cannot handle a branch to a target in
		  // a merged section.  If this is the case, issue an error
		  // and also free the merge symbol value.
		  if (!symval.has_output_value())
		    {
		      const std::string& section_name =
			object->section_name(shndx);
		      object->error(_("cannot handle branch to local %u "
					  "in a merged section %s"),
					r_sym, section_name.c_str());
		    }
		  psymval = &symval;
		}
	      else
		{
		  // We cannot determine the final value.
		  continue;
		}
	    }
	}
      else
	{
	  const Symbol* gsym;
	  gsym = object->global_symbol(r_sym);
	  gold_assert(gsym != NULL);
	  if (gsym->is_forwarder())
	    gsym = relinfo->symtab->resolve_forwards(gsym);

	  sym = static_cast<const Sized_symbol<size>*>(gsym);
	  if (sym->has_symtab_index() && sym->symtab_index() != -1U)
	    symval.set_output_symtab_index(sym->symtab_index());
	  else
	    symval.set_no_output_symtab_entry();

	  // We need to compute the would-be final value of this global
	  // symbol.
	  const Symbol_table* symtab = relinfo->symtab;
	  const Sized_symbol<size>* sized_symbol =
	      symtab->get_sized_symbol<size>(gsym);
	  Symbol_table::Compute_final_value_status status;
	  typename elfcpp::Elf_types<size>::Elf_Addr value =
	      symtab->compute_final_value<size>(sized_symbol, &status);

	  // Skip this if the symbol has not output section.
	  if (status == Symbol_table::CFVS_NO_OUTPUT_SECTION)
	    continue;
	  symval.set_output_value(value);

	  if (gsym->type() == elfcpp::STT_TLS)
	    symval.set_is_tls_symbol();
	  else if (gsym->type() == elfcpp::STT_GNU_IFUNC)
	    symval.set_is_ifunc_symbol();
	  psymval = &symval;

	  is_defined_in_discarded_section =
	      (gsym->is_defined_in_discarded_section()
	       && gsym->is_undefined());
	  shndx = 0;
	}

      Symbol_value<size> symval2;
      if (is_defined_in_discarded_section)
	{
	  if (comdat_behavior == CB_UNDETERMINED)
	    {
	      std::string name = object->section_name(relinfo->data_shndx);
	      comdat_behavior = default_comdat_behavior.get(name.c_str());
	    }
	  if (comdat_behavior == CB_PRETEND)
	    {
	      bool found;
	      typename elfcpp::Elf_types<size>::Elf_Addr value =
		object->map_to_kept_section(shndx, &found);
	      if (found)
		symval2.set_output_value(value + psymval->input_value());
	      else
		symval2.set_output_value(0);
	    }
	  else
	    {
	      if (comdat_behavior == CB_WARNING)
		gold_warning_at_location(relinfo, i, offset,
					 _("relocation refers to discarded "
					   "section"));
	      symval2.set_output_value(0);
	    }
	  symval2.set_no_output_symtab_entry();
	  psymval = &symval2;
	}

      // If symbol is a section symbol, we don't know the actual type of
      // destination.  Give up.
      if (psymval->is_section_symbol())
	continue;

      this->scan_reloc_for_stub(relinfo, r_type, sym, r_sym, psymval,
				addend, view_address + offset);
    }  // End of iterating relocs in a section
}  // End of Target_aarch64::scan_reloc_section_for_stubs


// Scan an input section for stub generation.

template<int size, bool big_endian>
void
Target_aarch64<size, big_endian>::scan_section_for_stubs(
    const Relocate_info<size, big_endian>* relinfo,
    unsigned int sh_type,
    const unsigned char* prelocs,
    size_t reloc_count,
    Output_section* output_section,
    bool needs_special_offset_handling,
    const unsigned char* view,
    Address view_address,
    section_size_type view_size)
{
  gold_assert(sh_type == elfcpp::SHT_RELA);
  this->scan_reloc_section_for_stubs<elfcpp::SHT_RELA>(
      relinfo,
      prelocs,
      reloc_count,
      output_section,
      needs_special_offset_handling,
      view,
      view_address,
      view_size);
}


// Relocate a single stub.

template<int size, bool big_endian>
void Target_aarch64<size, big_endian>::
relocate_stub(The_reloc_stub* stub,
	      const The_relocate_info*,
	      Output_section*,
	      unsigned char* view,
	      Address address,
	      section_size_type)
{
  typedef AArch64_relocate_functions<size, big_endian> The_reloc_functions;
  typedef typename The_reloc_functions::Status The_reloc_functions_status;
  typedef typename elfcpp::Swap<32,big_endian>::Valtype Insntype;

  Insntype* ip = reinterpret_cast<Insntype*>(view);
  int insn_number = stub->stub_insn_number();
  const uint32_t* insns = stub->stub_insns();
  // Check the insns are really those stub insns.
  for (int i = 0; i < insn_number; ++i)
    {
      Insntype insn = elfcpp::Swap<32,big_endian>::readval(ip + i);
      gold_assert(((uint32_t)insn == insns[i+1]));
    }

  Address dest = stub->destination_address();

  switch(stub->stub_type())
    {
    case The_reloc_stub::ST_ADRP_BRANCH:
      {
	// 1st reloc is ADR_PREL_PG_HI21
	The_reloc_functions_status status =
	    The_reloc_functions::adrp(view, dest, address);
	// An error should never arise in the above step. If so, please
	// check 'aarch64_valid_for_adrp_p'.
	gold_assert(status == The_reloc_functions::STATUS_OKAY);

	// 2nd reloc is ADD_ABS_LO12_NC
	const AArch64_reloc_property* arp =
	    aarch64_reloc_property_table->get_reloc_property(
		elfcpp::R_AARCH64_ADD_ABS_LO12_NC);
	gold_assert(arp != NULL);
	status = The_reloc_functions::template
	    rela_general<32>(view + 4, dest, 0, arp);
	// An error should never arise, it is an "_NC" relocation.
	gold_assert(status == The_reloc_functions::STATUS_OKAY);
      }
      break;

    case The_reloc_stub::ST_LONG_BRANCH_ABS:
      // 1st reloc is R_AARCH64_PREL64, at offset 8
      elfcpp::Swap<64,big_endian>::writeval(view + 8, dest);
      break;

    case The_reloc_stub::ST_LONG_BRANCH_PCREL:
      {
	// "PC" calculation is the 2nd insn in the stub.
	uint64_t offset = dest - (address + 4);
	// Offset is placed at offset 4 and 5.
	elfcpp::Swap<64,big_endian>::writeval(view + 16, offset);
      }
      break;

    default:
      gold_unreachable();
    }
}


// A class to handle the PLT data.
// This is an abstract base class that handles most of the linker details
// but does not know the actual contents of PLT entries.  The derived
// classes below fill in those details.

template<int size, bool big_endian>
class Output_data_plt_aarch64 : public Output_section_data
{
 public:
  typedef Output_data_reloc<elfcpp::SHT_RELA, true, size, big_endian>
      Reloc_section;
  typedef typename elfcpp::Elf_types<size>::Elf_Addr Address;

  Output_data_plt_aarch64(Layout* layout,
			  uint64_t addralign,
			  Output_data_got_aarch64<size, big_endian>* got,
			  Output_data_space* got_plt,
			  Output_data_space* got_irelative)
    : Output_section_data(addralign), tlsdesc_rel_(NULL), irelative_rel_(NULL),
      got_(got), got_plt_(got_plt), got_irelative_(got_irelative),
      count_(0), irelative_count_(0), tlsdesc_got_offset_(-1U)
  { this->init(layout); }

  // Initialize the PLT section.
  void
  init(Layout* layout);

  // Add an entry to the PLT.
  void
  add_entry(Symbol_table*, Layout*, Symbol* gsym);

  // Add an entry to the PLT for a local STT_GNU_IFUNC symbol.
  unsigned int
  add_local_ifunc_entry(Symbol_table* symtab, Layout*,
			Sized_relobj_file<size, big_endian>* relobj,
			unsigned int local_sym_index);

  // Add the relocation for a PLT entry.
  void
  add_relocation(Symbol_table*, Layout*, Symbol* gsym,
		 unsigned int got_offset);

  // Add the reserved TLSDESC_PLT entry to the PLT.
  void
  reserve_tlsdesc_entry(unsigned int got_offset)
  { this->tlsdesc_got_offset_ = got_offset; }

  // Return true if a TLSDESC_PLT entry has been reserved.
  bool
  has_tlsdesc_entry() const
  { return this->tlsdesc_got_offset_ != -1U; }

  // Return the GOT offset for the reserved TLSDESC_PLT entry.
  unsigned int
  get_tlsdesc_got_offset() const
  { return this->tlsdesc_got_offset_; }

  // Return the PLT offset of the reserved TLSDESC_PLT entry.
  unsigned int
  get_tlsdesc_plt_offset() const
  {
    return (this->first_plt_entry_offset() +
	    (this->count_ + this->irelative_count_)
	    * this->get_plt_entry_size());
  }

  // Return the .rela.plt section data.
  Reloc_section*
  rela_plt()
  { return this->rel_; }

  // Return where the TLSDESC relocations should go.
  Reloc_section*
  rela_tlsdesc(Layout*);

  // Return where the IRELATIVE relocations should go in the PLT
  // relocations.
  Reloc_section*
  rela_irelative(Symbol_table*, Layout*);

  // Return whether we created a section for IRELATIVE relocations.
  bool
  has_irelative_section() const
  { return this->irelative_rel_ != NULL; }

  // Return the number of PLT entries.
  unsigned int
  entry_count() const
  { return this->count_ + this->irelative_count_; }

  // Return the offset of the first non-reserved PLT entry.
  unsigned int
  first_plt_entry_offset() const
  { return this->do_first_plt_entry_offset(); }

  // Return the size of a PLT entry.
  unsigned int
  get_plt_entry_size() const
  { return this->do_get_plt_entry_size(); }

  // Return the reserved tlsdesc entry size.
  unsigned int
  get_plt_tlsdesc_entry_size() const
  { return this->do_get_plt_tlsdesc_entry_size(); }

  // Return the PLT address to use for a global symbol.
  uint64_t
  address_for_global(const Symbol*);

  // Return the PLT address to use for a local symbol.
  uint64_t
  address_for_local(const Relobj*, unsigned int symndx);

 protected:
  // Fill in the first PLT entry.
  void
  fill_first_plt_entry(unsigned char* pov,
		       Address got_address,
		       Address plt_address)
  { this->do_fill_first_plt_entry(pov, got_address, plt_address); }

  // Fill in a normal PLT entry.
  void
  fill_plt_entry(unsigned char* pov,
		 Address got_address,
		 Address plt_address,
		 unsigned int got_offset,
		 unsigned int plt_offset)
  {
    this->do_fill_plt_entry(pov, got_address, plt_address,
			    got_offset, plt_offset);
  }

  // Fill in the reserved TLSDESC PLT entry.
  void
  fill_tlsdesc_entry(unsigned char* pov,
		     Address gotplt_address,
		     Address plt_address,
		     Address got_base,
		     unsigned int tlsdesc_got_offset,
		     unsigned int plt_offset)
  {
    this->do_fill_tlsdesc_entry(pov, gotplt_address, plt_address, got_base,
				tlsdesc_got_offset, plt_offset);
  }

  virtual unsigned int
  do_first_plt_entry_offset() const = 0;

  virtual unsigned int
  do_get_plt_entry_size() const = 0;

  virtual unsigned int
  do_get_plt_tlsdesc_entry_size() const = 0;

  virtual void
  do_fill_first_plt_entry(unsigned char* pov,
			  Address got_addr,
			  Address plt_addr) = 0;

  virtual void
  do_fill_plt_entry(unsigned char* pov,
		    Address got_address,
		    Address plt_address,
		    unsigned int got_offset,
		    unsigned int plt_offset) = 0;

  virtual void
  do_fill_tlsdesc_entry(unsigned char* pov,
			Address gotplt_address,
			Address plt_address,
			Address got_base,
			unsigned int tlsdesc_got_offset,
			unsigned int plt_offset) = 0;

  void
  do_adjust_output_section(Output_section* os);

  // Write to a map file.
  void
  do_print_to_mapfile(Mapfile* mapfile) const
  { mapfile->print_output_data(this, _("** PLT")); }

 private:
  // Set the final size.
  void
  set_final_data_size();

  // Write out the PLT data.
  void
  do_write(Output_file*);

  // The reloc section.
  Reloc_section* rel_;

  // The TLSDESC relocs, if necessary.  These must follow the regular
  // PLT relocs.
  Reloc_section* tlsdesc_rel_;

  // The IRELATIVE relocs, if necessary.  These must follow the
  // regular PLT relocations.
  Reloc_section* irelative_rel_;

  // The .got section.
  Output_data_got_aarch64<size, big_endian>* got_;

  // The .got.plt section.
  Output_data_space* got_plt_;

  // The part of the .got.plt section used for IRELATIVE relocs.
  Output_data_space* got_irelative_;

  // The number of PLT entries.
  unsigned int count_;

  // Number of PLT entries with R_X86_64_IRELATIVE relocs.  These
  // follow the regular PLT entries.
  unsigned int irelative_count_;

  // GOT offset of the reserved TLSDESC_GOT entry for the lazy trampoline.
  // Communicated to the loader via DT_TLSDESC_GOT. The magic value -1
  // indicates an offset is not allocated.
  unsigned int tlsdesc_got_offset_;
};

// Initialize the PLT section.

template<int size, bool big_endian>
void
Output_data_plt_aarch64<size, big_endian>::init(Layout* layout)
{
  this->rel_ = new Reloc_section(false);
  layout->add_output_section_data(".rela.plt", elfcpp::SHT_RELA,
				  elfcpp::SHF_ALLOC, this->rel_,
				  ORDER_DYNAMIC_PLT_RELOCS, false);
}

template<int size, bool big_endian>
void
Output_data_plt_aarch64<size, big_endian>::do_adjust_output_section(
    Output_section* os)
{
  os->set_entsize(this->get_plt_entry_size());
}

// Add an entry to the PLT.

template<int size, bool big_endian>
void
Output_data_plt_aarch64<size, big_endian>::add_entry(Symbol_table* symtab,
    Layout* layout, Symbol* gsym)
{
  gold_assert(!gsym->has_plt_offset());

  unsigned int* pcount;
  unsigned int plt_reserved;
  Output_section_data_build* got;

  if (gsym->type() == elfcpp::STT_GNU_IFUNC
      && gsym->can_use_relative_reloc(false))
    {
      pcount = &this->irelative_count_;
      plt_reserved = 0;
      got = this->got_irelative_;
    }
  else
    {
      pcount = &this->count_;
      plt_reserved = this->first_plt_entry_offset();
      got = this->got_plt_;
    }

  gsym->set_plt_offset((*pcount) * this->get_plt_entry_size()
		       + plt_reserved);

  ++*pcount;

  section_offset_type got_offset = got->current_data_size();

  // Every PLT entry needs a GOT entry which points back to the PLT
  // entry (this will be changed by the dynamic linker, normally
  // lazily when the function is called).
  got->set_current_data_size(got_offset + size / 8);

  // Every PLT entry needs a reloc.
  this->add_relocation(symtab, layout, gsym, got_offset);

  // Note that we don't need to save the symbol. The contents of the
  // PLT are independent of which symbols are used. The symbols only
  // appear in the relocations.
}

// Add an entry to the PLT for a local STT_GNU_IFUNC symbol.  Return
// the PLT offset.

template<int size, bool big_endian>
unsigned int
Output_data_plt_aarch64<size, big_endian>::add_local_ifunc_entry(
    Symbol_table* symtab,
    Layout* layout,
    Sized_relobj_file<size, big_endian>* relobj,
    unsigned int local_sym_index)
{
  unsigned int plt_offset = this->irelative_count_ * this->get_plt_entry_size();
  ++this->irelative_count_;

  section_offset_type got_offset = this->got_irelative_->current_data_size();

  // Every PLT entry needs a GOT entry which points back to the PLT
  // entry.
  this->got_irelative_->set_current_data_size(got_offset + size / 8);

  // Every PLT entry needs a reloc.
  Reloc_section* rela = this->rela_irelative(symtab, layout);
  rela->add_symbolless_local_addend(relobj, local_sym_index,
				    elfcpp::R_AARCH64_IRELATIVE,
				    this->got_irelative_, got_offset, 0);

  return plt_offset;
}

// Add the relocation for a PLT entry.

template<int size, bool big_endian>
void
Output_data_plt_aarch64<size, big_endian>::add_relocation(
    Symbol_table* symtab, Layout* layout, Symbol* gsym, unsigned int got_offset)
{
  if (gsym->type() == elfcpp::STT_GNU_IFUNC
      && gsym->can_use_relative_reloc(false))
    {
      Reloc_section* rela = this->rela_irelative(symtab, layout);
      rela->add_symbolless_global_addend(gsym, elfcpp::R_AARCH64_IRELATIVE,
					 this->got_irelative_, got_offset, 0);
    }
  else
    {
      gsym->set_needs_dynsym_entry();
      this->rel_->add_global(gsym, elfcpp::R_AARCH64_JUMP_SLOT, this->got_plt_,
			     got_offset, 0);
    }
}

// Return where the TLSDESC relocations should go, creating it if
// necessary.  These follow the JUMP_SLOT relocations.

template<int size, bool big_endian>
typename Output_data_plt_aarch64<size, big_endian>::Reloc_section*
Output_data_plt_aarch64<size, big_endian>::rela_tlsdesc(Layout* layout)
{
  if (this->tlsdesc_rel_ == NULL)
    {
      this->tlsdesc_rel_ = new Reloc_section(false);
      layout->add_output_section_data(".rela.plt", elfcpp::SHT_RELA,
				      elfcpp::SHF_ALLOC, this->tlsdesc_rel_,
				      ORDER_DYNAMIC_PLT_RELOCS, false);
      gold_assert(this->tlsdesc_rel_->output_section()
		  == this->rel_->output_section());
    }
  return this->tlsdesc_rel_;
}

// Return where the IRELATIVE relocations should go in the PLT.  These
// follow the JUMP_SLOT and the TLSDESC relocations.

template<int size, bool big_endian>
typename Output_data_plt_aarch64<size, big_endian>::Reloc_section*
Output_data_plt_aarch64<size, big_endian>::rela_irelative(Symbol_table* symtab,
							  Layout* layout)
{
  if (this->irelative_rel_ == NULL)
    {
      // Make sure we have a place for the TLSDESC relocations, in
      // case we see any later on.
      this->rela_tlsdesc(layout);
      this->irelative_rel_ = new Reloc_section(false);
      layout->add_output_section_data(".rela.plt", elfcpp::SHT_RELA,
				      elfcpp::SHF_ALLOC, this->irelative_rel_,
				      ORDER_DYNAMIC_PLT_RELOCS, false);
      gold_assert(this->irelative_rel_->output_section()
		  == this->rel_->output_section());

      if (parameters->doing_static_link())
	{
	  // A statically linked executable will only have a .rela.plt
	  // section to hold R_AARCH64_IRELATIVE relocs for
	  // STT_GNU_IFUNC symbols.  The library will use these
	  // symbols to locate the IRELATIVE relocs at program startup
	  // time.
	  symtab->define_in_output_data("__rela_iplt_start", NULL,
					Symbol_table::PREDEFINED,
					this->irelative_rel_, 0, 0,
					elfcpp::STT_NOTYPE, elfcpp::STB_GLOBAL,
					elfcpp::STV_HIDDEN, 0, false, true);
	  symtab->define_in_output_data("__rela_iplt_end", NULL,
					Symbol_table::PREDEFINED,
					this->irelative_rel_, 0, 0,
					elfcpp::STT_NOTYPE, elfcpp::STB_GLOBAL,
					elfcpp::STV_HIDDEN, 0, true, true);
	}
    }
  return this->irelative_rel_;
}

// Return the PLT address to use for a global symbol.

template<int size, bool big_endian>
uint64_t
Output_data_plt_aarch64<size, big_endian>::address_for_global(
  const Symbol* gsym)
{
  uint64_t offset = 0;
  if (gsym->type() == elfcpp::STT_GNU_IFUNC
      && gsym->can_use_relative_reloc(false))
    offset = (this->first_plt_entry_offset() +
	      this->count_ * this->get_plt_entry_size());
  return this->address() + offset + gsym->plt_offset();
}

// Return the PLT address to use for a local symbol.  These are always
// IRELATIVE relocs.

template<int size, bool big_endian>
uint64_t
Output_data_plt_aarch64<size, big_endian>::address_for_local(
    const Relobj* object,
    unsigned int r_sym)
{
  return (this->address()
	  + this->first_plt_entry_offset()
	  + this->count_ * this->get_plt_entry_size()
	  + object->local_plt_offset(r_sym));
}

// Set the final size.

template<int size, bool big_endian>
void
Output_data_plt_aarch64<size, big_endian>::set_final_data_size()
{
  unsigned int count = this->count_ + this->irelative_count_;
  unsigned int extra_size = 0;
  if (this->has_tlsdesc_entry())
    extra_size += this->get_plt_tlsdesc_entry_size();
  this->set_data_size(this->first_plt_entry_offset()
		      + count * this->get_plt_entry_size()
		      + extra_size);
}

template<int size, bool big_endian>
class Output_data_plt_aarch64_standard :
  public Output_data_plt_aarch64<size, big_endian>
{
 public:
  typedef typename elfcpp::Elf_types<size>::Elf_Addr Address;
  Output_data_plt_aarch64_standard(
      Layout* layout,
      Output_data_got_aarch64<size, big_endian>* got,
      Output_data_space* got_plt,
      Output_data_space* got_irelative)
    : Output_data_plt_aarch64<size, big_endian>(layout,
						size == 32 ? 4 : 8,
						got, got_plt,
						got_irelative)
  { }

 protected:
  // Return the offset of the first non-reserved PLT entry.
  virtual unsigned int
  do_first_plt_entry_offset() const
  { return this->first_plt_entry_size; }

  // Return the size of a PLT entry
  virtual unsigned int
  do_get_plt_entry_size() const
  { return this->plt_entry_size; }

  // Return the size of a tlsdesc entry
  virtual unsigned int
  do_get_plt_tlsdesc_entry_size() const
  { return this->plt_tlsdesc_entry_size; }

  virtual void
  do_fill_first_plt_entry(unsigned char* pov,
			  Address got_address,
			  Address plt_address);

  virtual void
  do_fill_plt_entry(unsigned char* pov,
		    Address got_address,
		    Address plt_address,
		    unsigned int got_offset,
		    unsigned int plt_offset);

  virtual void
  do_fill_tlsdesc_entry(unsigned char* pov,
			Address gotplt_address,
			Address plt_address,
			Address got_base,
			unsigned int tlsdesc_got_offset,
			unsigned int plt_offset);

 private:
  // The size of the first plt entry size.
  static const int first_plt_entry_size = 32;
  // The size of the plt entry size.
  static const int plt_entry_size = 16;
  // The size of the plt tlsdesc entry size.
  static const int plt_tlsdesc_entry_size = 32;
  // Template for the first PLT entry.
  static const uint32_t first_plt_entry[first_plt_entry_size / 4];
  // Template for subsequent PLT entries.
  static const uint32_t plt_entry[plt_entry_size / 4];
  // The reserved TLSDESC entry in the PLT for an executable.
  static const uint32_t tlsdesc_plt_entry[plt_tlsdesc_entry_size / 4];
};

// The first entry in the PLT for an executable.

template<>
const uint32_t
Output_data_plt_aarch64_standard<32, false>::
    first_plt_entry[first_plt_entry_size / 4] =
{
  0xa9bf7bf0,	/* stp x16, x30, [sp, #-16]!  */
  0x90000010,	/* adrp x16, PLT_GOT+0x8  */
  0xb9400A11,	/* ldr w17, [x16, #PLT_GOT+0x8]  */
  0x11002210,	/* add w16, w16,#PLT_GOT+0x8   */
  0xd61f0220,	/* br x17  */
  0xd503201f,	/* nop */
  0xd503201f,	/* nop */
  0xd503201f,	/* nop */
};


template<>
const uint32_t
Output_data_plt_aarch64_standard<32, true>::
    first_plt_entry[first_plt_entry_size / 4] =
{
  0xa9bf7bf0,	/* stp x16, x30, [sp, #-16]!  */
  0x90000010,	/* adrp x16, PLT_GOT+0x8  */
  0xb9400A11,	/* ldr w17, [x16, #PLT_GOT+0x8]  */
  0x11002210,	/* add w16, w16,#PLT_GOT+0x8   */
  0xd61f0220,	/* br x17  */
  0xd503201f,	/* nop */
  0xd503201f,	/* nop */
  0xd503201f,	/* nop */
};


template<>
const uint32_t
Output_data_plt_aarch64_standard<64, false>::
    first_plt_entry[first_plt_entry_size / 4] =
{
  0xa9bf7bf0,	/* stp x16, x30, [sp, #-16]!  */
  0x90000010,	/* adrp x16, PLT_GOT+16  */
  0xf9400A11,	/* ldr x17, [x16, #PLT_GOT+0x10]  */
  0x91004210,	/* add x16, x16,#PLT_GOT+0x10   */
  0xd61f0220,	/* br x17  */
  0xd503201f,	/* nop */
  0xd503201f,	/* nop */
  0xd503201f,	/* nop */
};


template<>
const uint32_t
Output_data_plt_aarch64_standard<64, true>::
    first_plt_entry[first_plt_entry_size / 4] =
{
  0xa9bf7bf0,	/* stp x16, x30, [sp, #-16]!  */
  0x90000010,	/* adrp x16, PLT_GOT+16  */
  0xf9400A11,	/* ldr x17, [x16, #PLT_GOT+0x10]  */
  0x91004210,	/* add x16, x16,#PLT_GOT+0x10   */
  0xd61f0220,	/* br x17  */
  0xd503201f,	/* nop */
  0xd503201f,	/* nop */
  0xd503201f,	/* nop */
};


template<>
const uint32_t
Output_data_plt_aarch64_standard<32, false>::
    plt_entry[plt_entry_size / 4] =
{
  0x90000010,	/* adrp x16, PLTGOT + n * 4  */
  0xb9400211,	/* ldr w17, [w16, PLTGOT + n * 4] */
  0x11000210,	/* add w16, w16, :lo12:PLTGOT + n * 4  */
  0xd61f0220,	/* br x17.  */
};


template<>
const uint32_t
Output_data_plt_aarch64_standard<32, true>::
    plt_entry[plt_entry_size / 4] =
{
  0x90000010,	/* adrp x16, PLTGOT + n * 4  */
  0xb9400211,	/* ldr w17, [w16, PLTGOT + n * 4] */
  0x11000210,	/* add w16, w16, :lo12:PLTGOT + n * 4  */
  0xd61f0220,	/* br x17.  */
};


template<>
const uint32_t
Output_data_plt_aarch64_standard<64, false>::
    plt_entry[plt_entry_size / 4] =
{
  0x90000010,	/* adrp x16, PLTGOT + n * 8  */
  0xf9400211,	/* ldr x17, [x16, PLTGOT + n * 8] */
  0x91000210,	/* add x16, x16, :lo12:PLTGOT + n * 8  */
  0xd61f0220,	/* br x17.  */
};


template<>
const uint32_t
Output_data_plt_aarch64_standard<64, true>::
    plt_entry[plt_entry_size / 4] =
{
  0x90000010,	/* adrp x16, PLTGOT + n * 8  */
  0xf9400211,	/* ldr x17, [x16, PLTGOT + n * 8] */
  0x91000210,	/* add x16, x16, :lo12:PLTGOT + n * 8  */
  0xd61f0220,	/* br x17.  */
};


template<int size, bool big_endian>
void
Output_data_plt_aarch64_standard<size, big_endian>::do_fill_first_plt_entry(
    unsigned char* pov,
    Address got_address,
    Address plt_address)
{
  // PLT0 of the small PLT looks like this in ELF64 -
  // stp x16, x30, [sp, #-16]!	 	Save the reloc and lr on stack.
  // adrp x16, PLT_GOT + 16		Get the page base of the GOTPLT
  // ldr  x17, [x16, #:lo12:PLT_GOT+16]	Load the address of the
  // 					symbol resolver
  // add  x16, x16, #:lo12:PLT_GOT+16	Load the lo12 bits of the
  // 					GOTPLT entry for this.
  // br   x17
  // PLT0 will be slightly different in ELF32 due to different got entry
  // size.
  memcpy(pov, this->first_plt_entry, this->first_plt_entry_size);
  Address gotplt_2nd_ent = got_address + (size / 8) * 2;

  // Fill in the top 21 bits for this: ADRP x16, PLT_GOT + 8 * 2.
  // ADRP:  (PG(S+A)-PG(P)) >> 12) & 0x1fffff.
  // FIXME: This only works for 64bit
  AArch64_relocate_functions<size, big_endian>::adrp(pov + 4,
      gotplt_2nd_ent, plt_address + 4);

  // Fill in R_AARCH64_LDST8_LO12
  elfcpp::Swap<32, big_endian>::writeval(
      pov + 8,
      ((this->first_plt_entry[2] & 0xffc003ff)
       | ((gotplt_2nd_ent & 0xff8) << 7)));

  // Fill in R_AARCH64_ADD_ABS_LO12
  elfcpp::Swap<32, big_endian>::writeval(
      pov + 12,
      ((this->first_plt_entry[3] & 0xffc003ff)
       | ((gotplt_2nd_ent & 0xfff) << 10)));
}


// Subsequent entries in the PLT for an executable.
// FIXME: This only works for 64bit

template<int size, bool big_endian>
void
Output_data_plt_aarch64_standard<size, big_endian>::do_fill_plt_entry(
    unsigned char* pov,
    Address got_address,
    Address plt_address,
    unsigned int got_offset,
    unsigned int plt_offset)
{
  memcpy(pov, this->plt_entry, this->plt_entry_size);

  Address gotplt_entry_address = got_address + got_offset;
  Address plt_entry_address = plt_address + plt_offset;

  // Fill in R_AARCH64_PCREL_ADR_HI21
  AArch64_relocate_functions<size, big_endian>::adrp(
      pov,
      gotplt_entry_address,
      plt_entry_address);

  // Fill in R_AARCH64_LDST64_ABS_LO12
  elfcpp::Swap<32, big_endian>::writeval(
      pov + 4,
      ((this->plt_entry[1] & 0xffc003ff)
       | ((gotplt_entry_address & 0xff8) << 7)));

  // Fill in R_AARCH64_ADD_ABS_LO12
  elfcpp::Swap<32, big_endian>::writeval(
      pov + 8,
      ((this->plt_entry[2] & 0xffc003ff)
       | ((gotplt_entry_address & 0xfff) <<10)));

}


template<>
const uint32_t
Output_data_plt_aarch64_standard<32, false>::
    tlsdesc_plt_entry[plt_tlsdesc_entry_size / 4] =
{
  0xa9bf0fe2,	/* stp x2, x3, [sp, #-16]!  */
  0x90000002,	/* adrp x2, 0 */
  0x90000003,	/* adrp x3, 0 */
  0xb9400042,	/* ldr w2, [w2, #0] */
  0x11000063,	/* add w3, w3, 0 */
  0xd61f0040,	/* br x2 */
  0xd503201f,	/* nop */
  0xd503201f,	/* nop */
};

template<>
const uint32_t
Output_data_plt_aarch64_standard<32, true>::
    tlsdesc_plt_entry[plt_tlsdesc_entry_size / 4] =
{
  0xa9bf0fe2,	/* stp x2, x3, [sp, #-16]!  */
  0x90000002,	/* adrp x2, 0 */
  0x90000003,	/* adrp x3, 0 */
  0xb9400042,	/* ldr w2, [w2, #0] */
  0x11000063,	/* add w3, w3, 0 */
  0xd61f0040,	/* br x2 */
  0xd503201f,	/* nop */
  0xd503201f,	/* nop */
};

template<>
const uint32_t
Output_data_plt_aarch64_standard<64, false>::
    tlsdesc_plt_entry[plt_tlsdesc_entry_size / 4] =
{
  0xa9bf0fe2,	/* stp x2, x3, [sp, #-16]!  */
  0x90000002,	/* adrp x2, 0 */
  0x90000003,	/* adrp x3, 0 */
  0xf9400042,	/* ldr x2, [x2, #0] */
  0x91000063,	/* add x3, x3, 0 */
  0xd61f0040,	/* br x2 */
  0xd503201f,	/* nop */
  0xd503201f,	/* nop */
};

template<>
const uint32_t
Output_data_plt_aarch64_standard<64, true>::
    tlsdesc_plt_entry[plt_tlsdesc_entry_size / 4] =
{
  0xa9bf0fe2,	/* stp x2, x3, [sp, #-16]!  */
  0x90000002,	/* adrp x2, 0 */
  0x90000003,	/* adrp x3, 0 */
  0xf9400042,	/* ldr x2, [x2, #0] */
  0x91000063,	/* add x3, x3, 0 */
  0xd61f0040,	/* br x2 */
  0xd503201f,	/* nop */
  0xd503201f,	/* nop */
};

template<int size, bool big_endian>
void
Output_data_plt_aarch64_standard<size, big_endian>::do_fill_tlsdesc_entry(
    unsigned char* pov,
    Address gotplt_address,
    Address plt_address,
    Address got_base,
    unsigned int tlsdesc_got_offset,
    unsigned int plt_offset)
{
  memcpy(pov, tlsdesc_plt_entry, plt_tlsdesc_entry_size);

  // move DT_TLSDESC_GOT address into x2
  // move .got.plt address into x3
  Address tlsdesc_got_entry = got_base + tlsdesc_got_offset;
  Address plt_entry_address = plt_address + plt_offset;

  // R_AARCH64_ADR_PREL_PG_HI21
  AArch64_relocate_functions<size, big_endian>::adrp(
      pov + 4,
      tlsdesc_got_entry,
      plt_entry_address + 4);

  // R_AARCH64_ADR_PREL_PG_HI21
  AArch64_relocate_functions<size, big_endian>::adrp(
      pov + 8,
      gotplt_address,
      plt_entry_address + 8);

  // R_AARCH64_LDST64_ABS_LO12
  elfcpp::Swap<32, big_endian>::writeval(
      pov + 12,
      ((this->tlsdesc_plt_entry[3] & 0xffc003ff)
       | ((tlsdesc_got_entry & 0xff8) << 7)));

  // R_AARCH64_ADD_ABS_LO12
  elfcpp::Swap<32, big_endian>::writeval(
      pov + 16,
      ((this->tlsdesc_plt_entry[4] & 0xffc003ff)
       | ((gotplt_address & 0xfff) << 10)));
}

// Write out the PLT.  This uses the hand-coded instructions above,
// and adjusts them as needed.  This is specified by the AMD64 ABI.

template<int size, bool big_endian>
void
Output_data_plt_aarch64<size, big_endian>::do_write(Output_file* of)
{
  const off_t offset = this->offset();
  const section_size_type oview_size =
    convert_to_section_size_type(this->data_size());
  unsigned char* const oview = of->get_output_view(offset, oview_size);

  const off_t got_file_offset = this->got_plt_->offset();
  gold_assert(got_file_offset + this->got_plt_->data_size()
	      == this->got_irelative_->offset());

  const section_size_type got_size =
      convert_to_section_size_type(this->got_plt_->data_size()
				   + this->got_irelative_->data_size());
  unsigned char* const got_view = of->get_output_view(got_file_offset,
						      got_size);

  unsigned char* pov = oview;

  // The base address of the .plt section.
  typename elfcpp::Elf_types<size>::Elf_Addr plt_address = this->address();
  // The base address of the PLT portion of the .got section.
  typename elfcpp::Elf_types<size>::Elf_Addr gotplt_address
      = this->got_plt_->address();

  this->fill_first_plt_entry(pov, gotplt_address, plt_address);
  pov += this->first_plt_entry_offset();

  // The first three entries in .got.plt are reserved.
  unsigned char* got_pov = got_view;
  memset(got_pov, 0, size / 8 * AARCH64_GOTPLT_RESERVE_COUNT);
  got_pov += (size / 8) * AARCH64_GOTPLT_RESERVE_COUNT;

  unsigned int plt_offset = this->first_plt_entry_offset();
  unsigned int got_offset = (size / 8) * AARCH64_GOTPLT_RESERVE_COUNT;
  const unsigned int count = this->count_ + this->irelative_count_;
  for (unsigned int plt_index = 0;
       plt_index < count;
       ++plt_index,
	 pov += this->get_plt_entry_size(),
	 got_pov += size / 8,
	 plt_offset += this->get_plt_entry_size(),
	 got_offset += size / 8)
    {
      // Set and adjust the PLT entry itself.
      this->fill_plt_entry(pov, gotplt_address, plt_address,
			   got_offset, plt_offset);

      // Set the entry in the GOT, which points to plt0.
      elfcpp::Swap<size, big_endian>::writeval(got_pov, plt_address);
    }

  if (this->has_tlsdesc_entry())
    {
      // Set and adjust the reserved TLSDESC PLT entry.
      unsigned int tlsdesc_got_offset = this->get_tlsdesc_got_offset();
      // The base address of the .base section.
      typename elfcpp::Elf_types<size>::Elf_Addr got_base =
	  this->got_->address();
      this->fill_tlsdesc_entry(pov, gotplt_address, plt_address, got_base,
			       tlsdesc_got_offset, plt_offset);
      pov += this->get_plt_tlsdesc_entry_size();
    }

  gold_assert(static_cast<section_size_type>(pov - oview) == oview_size);
  gold_assert(static_cast<section_size_type>(got_pov - got_view) == got_size);

  of->write_output_view(offset, oview_size, oview);
  of->write_output_view(got_file_offset, got_size, got_view);
}

// Telling how to update the immediate field of an instruction.
struct AArch64_howto
{
  // The immediate field mask.
  elfcpp::Elf_Xword dst_mask;

  // The offset to apply relocation immediate
  int doffset;

  // The second part offset, if the immediate field has two parts.
  // -1 if the immediate field has only one part.
  int doffset2;
};

static const AArch64_howto aarch64_howto[AArch64_reloc_property::INST_NUM] =
{
  {0, -1, -1},		// DATA
  {0x1fffe0, 5, -1},	// MOVW  [20:5]-imm16
  {0xffffe0, 5, -1},	// LD    [23:5]-imm19
  {0x60ffffe0, 29, 5},	// ADR   [30:29]-immlo  [23:5]-immhi
  {0x60ffffe0, 29, 5},	// ADRP  [30:29]-immlo  [23:5]-immhi
  {0x3ffc00, 10, -1},	// ADD   [21:10]-imm12
  {0x3ffc00, 10, -1},	// LDST  [21:10]-imm12
  {0x7ffe0, 5, -1},	// TBZNZ [18:5]-imm14
  {0xffffe0, 5, -1},	// CONDB [23:5]-imm19
  {0x3ffffff, 0, -1},	// B     [25:0]-imm26
  {0x3ffffff, 0, -1},	// CALL  [25:0]-imm26
};

// AArch64 relocate function class

template<int size, bool big_endian>
class AArch64_relocate_functions
{
 public:
  typedef enum
  {
    STATUS_OKAY,	// No error during relocation.
    STATUS_OVERFLOW,	// Relocation overflow.
    STATUS_BAD_RELOC,	// Relocation cannot be applied.
  } Status;

  typedef AArch64_relocate_functions<size, big_endian> This;
  typedef typename elfcpp::Elf_types<size>::Elf_Addr Address;
  typedef Relocate_info<size, big_endian> The_relocate_info;
  typedef AArch64_relobj<size, big_endian> The_aarch64_relobj;
  typedef Reloc_stub<size, big_endian> The_reloc_stub;
  typedef typename The_reloc_stub::Stub_type The_reloc_stub_type;
  typedef Stub_table<size, big_endian> The_stub_table;
  typedef elfcpp::Rela<size, big_endian> The_rela;
  typedef typename elfcpp::Swap<size, big_endian>::Valtype AArch64_valtype;

  // Return the page address of the address.
  // Page(address) = address & ~0xFFF

  static inline AArch64_valtype
  Page(Address address)
  {
    return (address & (~static_cast<Address>(0xFFF)));
  }

 private:
  // Update instruction (pointed by view) with selected bits (immed).
  // val = (val & ~dst_mask) | (immed << doffset)

  template<int valsize>
  static inline void
  update_view(unsigned char* view,
	      AArch64_valtype immed,
	      elfcpp::Elf_Xword doffset,
	      elfcpp::Elf_Xword dst_mask)
  {
    typedef typename elfcpp::Swap<valsize, big_endian>::Valtype Valtype;
    Valtype* wv = reinterpret_cast<Valtype*>(view);
    Valtype val = elfcpp::Swap<valsize, big_endian>::readval(wv);

    // Clear immediate fields.
    val &= ~dst_mask;
    elfcpp::Swap<valsize, big_endian>::writeval(wv,
      static_cast<Valtype>(val | (immed << doffset)));
  }

  // Update two parts of an instruction (pointed by view) with selected
  // bits (immed1 and immed2).
  // val = (val & ~dst_mask) | (immed1 << doffset1) | (immed2 << doffset2)

  template<int valsize>
  static inline void
  update_view_two_parts(
    unsigned char* view,
    AArch64_valtype immed1,
    AArch64_valtype immed2,
    elfcpp::Elf_Xword doffset1,
    elfcpp::Elf_Xword doffset2,
    elfcpp::Elf_Xword dst_mask)
  {
    typedef typename elfcpp::Swap<valsize, big_endian>::Valtype Valtype;
    Valtype* wv = reinterpret_cast<Valtype*>(view);
    Valtype val = elfcpp::Swap<valsize, big_endian>::readval(wv);
    val &= ~dst_mask;
    elfcpp::Swap<valsize, big_endian>::writeval(wv,
      static_cast<Valtype>(val | (immed1 << doffset1) |
			   (immed2 << doffset2)));
  }

  // Update adr or adrp instruction with immed.
  // In adr and adrp: [30:29] immlo   [23:5] immhi

  static inline void
  update_adr(unsigned char* view, AArch64_valtype immed)
  {
    elfcpp::Elf_Xword dst_mask = (0x3 << 29) | (0x7ffff << 5);
    This::template update_view_two_parts<32>(
      view,
      immed & 0x3,
      (immed & 0x1ffffc) >> 2,
      29,
      5,
      dst_mask);
  }

  // Update movz/movn instruction with bits immed.
  // Set instruction to movz if is_movz is true, otherwise set instruction
  // to movn.

  static inline void
  update_movnz(unsigned char* view,
	       AArch64_valtype immed,
	       bool is_movz)
  {
    typedef typename elfcpp::Swap<32, big_endian>::Valtype Valtype;
    Valtype* wv = reinterpret_cast<Valtype*>(view);
    Valtype val = elfcpp::Swap<32, big_endian>::readval(wv);

    const elfcpp::Elf_Xword doffset =
	aarch64_howto[AArch64_reloc_property::INST_MOVW].doffset;
    const elfcpp::Elf_Xword dst_mask =
	aarch64_howto[AArch64_reloc_property::INST_MOVW].dst_mask;

    // Clear immediate fields and opc code.
    val &= ~(dst_mask | (0x3 << 29));

    // Set instruction to movz or movn.
    // movz: [30:29] is 10   movn: [30:29] is 00
    if (is_movz)
      val |= (0x2 << 29);

    elfcpp::Swap<32, big_endian>::writeval(wv,
      static_cast<Valtype>(val | (immed << doffset)));
  }

  // Update selected bits in text.

  template<int valsize>
  static inline typename This::Status
  reloc_common(unsigned char* view, Address x,
		const AArch64_reloc_property* reloc_property)
  {
    // Select bits from X.
    Address immed = reloc_property->select_x_value(x);

    // Update view.
    const AArch64_reloc_property::Reloc_inst inst =
      reloc_property->reloc_inst();
    // If it is a data relocation or instruction has 2 parts of immediate
    // fields, you should not call pcrela_general.
    gold_assert(aarch64_howto[inst].doffset2 == -1 &&
		aarch64_howto[inst].doffset != -1);
    This::template update_view<valsize>(view, immed,
					aarch64_howto[inst].doffset,
					aarch64_howto[inst].dst_mask);

    // Do check overflow or alignment if needed.
    return (reloc_property->checkup_x_value(x)
	    ? This::STATUS_OKAY
	    : This::STATUS_OVERFLOW);
  }

 public:

  // Do a simple rela relocation at unaligned addresses.

  template<int valsize>
  static inline typename This::Status
  rela_ua(unsigned char* view,
	  const Sized_relobj_file<size, big_endian>* object,
	  const Symbol_value<size>* psymval,
	  AArch64_valtype addend,
	  const AArch64_reloc_property* reloc_property)
  {
    typedef typename elfcpp::Swap_unaligned<valsize, big_endian>::Valtype
      Valtype;
    typename elfcpp::Elf_types<size>::Elf_Addr x =
	psymval->value(object, addend);
    elfcpp::Swap_unaligned<valsize, big_endian>::writeval(view,
      static_cast<Valtype>(x));
    return (reloc_property->checkup_x_value(x)
	    ? This::STATUS_OKAY
	    : This::STATUS_OVERFLOW);
  }

  // Do a simple pc-relative relocation at unaligned addresses.

  template<int valsize>
  static inline typename This::Status
  pcrela_ua(unsigned char* view,
	    const Sized_relobj_file<size, big_endian>* object,
	    const Symbol_value<size>* psymval,
	    AArch64_valtype addend,
	    Address address,
	    const AArch64_reloc_property* reloc_property)
  {
    typedef typename elfcpp::Swap_unaligned<valsize, big_endian>::Valtype
      Valtype;
    Address x = psymval->value(object, addend) - address;
    elfcpp::Swap_unaligned<valsize, big_endian>::writeval(view,
      static_cast<Valtype>(x));
    return (reloc_property->checkup_x_value(x)
	    ? This::STATUS_OKAY
	    : This::STATUS_OVERFLOW);
  }

  // Do a simple rela relocation at aligned addresses.

  template<int valsize>
  static inline typename This::Status
  rela(
    unsigned char* view,
    const Sized_relobj_file<size, big_endian>* object,
    const Symbol_value<size>* psymval,
    AArch64_valtype addend,
    const AArch64_reloc_property* reloc_property)
  {
    typedef typename elfcpp::Swap<valsize, big_endian>::Valtype Valtype;
    Valtype* wv = reinterpret_cast<Valtype*>(view);
    Address x = psymval->value(object, addend);
    elfcpp::Swap<valsize, big_endian>::writeval(wv,static_cast<Valtype>(x));
    return (reloc_property->checkup_x_value(x)
	    ? This::STATUS_OKAY
	    : This::STATUS_OVERFLOW);
  }

  // Do relocate. Update selected bits in text.
  // new_val = (val & ~dst_mask) | (immed << doffset)

  template<int valsize>
  static inline typename This::Status
  rela_general(unsigned char* view,
	       const Sized_relobj_file<size, big_endian>* object,
	       const Symbol_value<size>* psymval,
	       AArch64_valtype addend,
	       const AArch64_reloc_property* reloc_property)
  {
    // Calculate relocation.
    Address x = psymval->value(object, addend);
    return This::template reloc_common<valsize>(view, x, reloc_property);
  }

  // Do relocate. Update selected bits in text.
  // new val = (val & ~dst_mask) | (immed << doffset)

  template<int valsize>
  static inline typename This::Status
  rela_general(
    unsigned char* view,
    AArch64_valtype s,
    AArch64_valtype addend,
    const AArch64_reloc_property* reloc_property)
  {
    // Calculate relocation.
    Address x = s + addend;
    return This::template reloc_common<valsize>(view, x, reloc_property);
  }

  // Do address relative relocate. Update selected bits in text.
  // new val = (val & ~dst_mask) | (immed << doffset)

  template<int valsize>
  static inline typename This::Status
  pcrela_general(
    unsigned char* view,
    const Sized_relobj_file<size, big_endian>* object,
    const Symbol_value<size>* psymval,
    AArch64_valtype addend,
    Address address,
    const AArch64_reloc_property* reloc_property)
  {
    // Calculate relocation.
    Address x = psymval->value(object, addend) - address;
    return This::template reloc_common<valsize>(view, x, reloc_property);
  }


  // Calculate (S + A) - address, update adr instruction.

  static inline typename This::Status
  adr(unsigned char* view,
      const Sized_relobj_file<size, big_endian>* object,
      const Symbol_value<size>* psymval,
      Address addend,
      Address address,
      const AArch64_reloc_property* /* reloc_property */)
  {
    AArch64_valtype x = psymval->value(object, addend) - address;
    // Pick bits [20:0] of X.
    AArch64_valtype immed = x & 0x1fffff;
    update_adr(view, immed);
    // Check -2^20 <= X < 2^20
    return (size == 64 && Bits<21>::has_overflow((x))
	    ? This::STATUS_OVERFLOW
	    : This::STATUS_OKAY);
  }

  // Calculate PG(S+A) - PG(address), update adrp instruction.
  // R_AARCH64_ADR_PREL_PG_HI21

  static inline typename This::Status
  adrp(
    unsigned char* view,
    Address sa,
    Address address)
  {
    AArch64_valtype x = This::Page(sa) - This::Page(address);
    // Pick [32:12] of X.
    AArch64_valtype immed = (x >> 12) & 0x1fffff;
    update_adr(view, immed);
    // Check -2^32 <= X < 2^32
    return (size == 64 && Bits<33>::has_overflow((x))
	    ? This::STATUS_OVERFLOW
	    : This::STATUS_OKAY);
  }

  // Calculate PG(S+A) - PG(address), update adrp instruction.
  // R_AARCH64_ADR_PREL_PG_HI21

  static inline typename This::Status
  adrp(unsigned char* view,
       const Sized_relobj_file<size, big_endian>* object,
       const Symbol_value<size>* psymval,
       Address addend,
       Address address,
       const AArch64_reloc_property* reloc_property)
  {
    Address sa = psymval->value(object, addend);
    AArch64_valtype x = This::Page(sa) - This::Page(address);
    // Pick [32:12] of X.
    AArch64_valtype immed = (x >> 12) & 0x1fffff;
    update_adr(view, immed);
    return (reloc_property->checkup_x_value(x)
	    ? This::STATUS_OKAY
	    : This::STATUS_OVERFLOW);
  }

  // Update mov[n/z] instruction. Check overflow if needed.
  // If X >=0, set the instruction to movz and its immediate value to the
  // selected bits S.
  // If X < 0, set the instruction to movn and its immediate value to
  // NOT (selected bits of).

  static inline typename This::Status
  movnz(unsigned char* view,
	AArch64_valtype x,
	const AArch64_reloc_property* reloc_property)
  {
    // Select bits from X.
    Address immed;
    bool is_movz;
    typedef typename elfcpp::Elf_types<size>::Elf_Swxword SignedW;
    if (static_cast<SignedW>(x) >= 0)
      {
	immed = reloc_property->select_x_value(x);
        is_movz = true;
      }
    else
      {
	immed = reloc_property->select_x_value(~x);;
	is_movz = false;
      }

    // Update movnz instruction.
    update_movnz(view, immed, is_movz);

    // Do check overflow or alignment if needed.
    return (reloc_property->checkup_x_value(x)
	    ? This::STATUS_OKAY
	    : This::STATUS_OVERFLOW);
  }

  static inline bool
  maybe_apply_stub(unsigned int,
		   const The_relocate_info*,
		   const The_rela&,
		   unsigned char*,
		   Address,
		   const Sized_symbol<size>*,
		   const Symbol_value<size>*,
		   const Sized_relobj_file<size, big_endian>*);

};  // End of AArch64_relocate_functions


// For a certain relocation type (usually jump/branch), test to see if the
// destination needs a stub to fulfil. If so, re-route the destination of the
// original instruction to the stub, note, at this time, the stub has already
// been generated.

template<int size, bool big_endian>
bool
AArch64_relocate_functions<size, big_endian>::
maybe_apply_stub(unsigned int r_type,
		 const The_relocate_info* relinfo,
		 const The_rela& rela,
		 unsigned char* view,
		 Address address,
		 const Sized_symbol<size>* gsym,
		 const Symbol_value<size>* psymval,
		 const Sized_relobj_file<size, big_endian>* object)
{
  if (parameters->options().relocatable())
    return false;

  typename elfcpp::Elf_types<size>::Elf_Swxword addend = rela.get_r_addend();
  Address branch_target = psymval->value(object, 0) + addend;
  The_reloc_stub_type stub_type = The_reloc_stub::
    stub_type_for_reloc(r_type, address, branch_target);
  if (stub_type == The_reloc_stub::ST_NONE)
    return false;

  const The_aarch64_relobj* aarch64_relobj =
      static_cast<const The_aarch64_relobj*>(object);
  The_stub_table* stub_table = aarch64_relobj->stub_table(relinfo->data_shndx);
  gold_assert(stub_table != NULL);

  unsigned int r_sym = elfcpp::elf_r_sym<size>(rela.get_r_info());
  typename The_reloc_stub::Key stub_key(stub_type, gsym, object, r_sym, addend);
  The_reloc_stub* stub = stub_table->find_reloc_stub(stub_key);
  gold_assert(stub != NULL);

  Address new_branch_target = stub_table->address() + stub->offset();
  typename elfcpp::Swap<size, big_endian>::Valtype branch_offset =
      new_branch_target - address;
  const AArch64_reloc_property* arp =
      aarch64_reloc_property_table->get_reloc_property(r_type);
  gold_assert(arp != NULL);
  This::Status status = This::template
      rela_general<32>(view, branch_offset, 0, arp);
  if (status != This::STATUS_OKAY)
    gold_error(_("Stub is too far away, try a smaller value "
		 "for '--stub-group-size'. For example, 0x2000000."));
  return true;
}


// Group input sections for stub generation.
//
// We group input sections in an output section so that the total size,
// including any padding space due to alignment is smaller than GROUP_SIZE
// unless the only input section in group is bigger than GROUP_SIZE already.
// Then an ARM stub table is created to follow the last input section
// in group.  For each group an ARM stub table is created an is placed
// after the last group.  If STUB_ALWAYS_AFTER_BRANCH is false, we further
// extend the group after the stub table.

template<int size, bool big_endian>
void
Target_aarch64<size, big_endian>::group_sections(
    Layout* layout,
    section_size_type group_size,
    bool stubs_always_after_branch,
    const Task* task)
{
  // Group input sections and insert stub table
  Layout::Section_list section_list;
  layout->get_executable_sections(&section_list);
  for (Layout::Section_list::const_iterator p = section_list.begin();
       p != section_list.end();
       ++p)
    {
      AArch64_output_section<size, big_endian>* output_section =
	  static_cast<AArch64_output_section<size, big_endian>*>(*p);
      output_section->group_sections(group_size, stubs_always_after_branch,
				     this, task);
    }
}


// Find the AArch64_input_section object corresponding to the SHNDX-th input
// section of RELOBJ.

template<int size, bool big_endian>
AArch64_input_section<size, big_endian>*
Target_aarch64<size, big_endian>::find_aarch64_input_section(
    Relobj* relobj, unsigned int shndx) const
{
  Section_id sid(relobj, shndx);
  typename AArch64_input_section_map::const_iterator p =
    this->aarch64_input_section_map_.find(sid);
  return (p != this->aarch64_input_section_map_.end()) ? p->second : NULL;
}


// Make a new AArch64_input_section object.

template<int size, bool big_endian>
AArch64_input_section<size, big_endian>*
Target_aarch64<size, big_endian>::new_aarch64_input_section(
    Relobj* relobj, unsigned int shndx)
{
  Section_id sid(relobj, shndx);

  AArch64_input_section<size, big_endian>* input_section =
      new AArch64_input_section<size, big_endian>(relobj, shndx);
  input_section->init();

  // Register new AArch64_input_section in map for look-up.
  std::pair<typename AArch64_input_section_map::iterator,bool> ins =
      this->aarch64_input_section_map_.insert(
	  std::make_pair(sid, input_section));

  // Make sure that it we have not created another AArch64_input_section
  // for this input section already.
  gold_assert(ins.second);

  return input_section;
}


// Relaxation hook.  This is where we do stub generation.

template<int size, bool big_endian>
bool
Target_aarch64<size, big_endian>::do_relax(
    int pass,
    const Input_objects* input_objects,
    Symbol_table* symtab,
    Layout* layout ,
    const Task* task)
{
  gold_assert(!parameters->options().relocatable());
  if (pass == 1)
    {
      section_size_type stub_group_size =
	  parameters->options().stub_group_size();
      if (stub_group_size == 1)
	{
	  // Leave room for 4096 4-byte stub entries. If we exceed that, then we
	  // will fail to link.  The user will have to relink with an explicit
	  // group size option.
	  stub_group_size = The_reloc_stub::MAX_BRANCH_OFFSET - 4096 * 4;
	}
      group_sections(layout, stub_group_size, true, task);
    }
  else
    {
      // If this is not the first pass, addresses and file offsets have
      // been reset at this point, set them here.
      for (Stub_table_iterator sp = this->stub_tables_.begin();
	   sp != this->stub_tables_.end(); ++sp)
	{
	  The_stub_table* stt = *sp;
	  The_aarch64_input_section* owner = stt->owner();
	  off_t off = align_address(owner->original_size(),
				    stt->addralign());
	  stt->set_address_and_file_offset(owner->address() + off,
					   owner->offset() + off);
	}
    }

  // Scan relocs for relocation stubs
  for (Input_objects::Relobj_iterator op = input_objects->relobj_begin();
       op != input_objects->relobj_end();
       ++op)
    {
      The_aarch64_relobj* aarch64_relobj =
	  static_cast<The_aarch64_relobj*>(*op);
      // Lock the object so we can read from it.  This is only called
      // single-threaded from Layout::finalize, so it is OK to lock.
      Task_lock_obj<Object> tl(task, aarch64_relobj);
      aarch64_relobj->scan_sections_for_stubs(this, symtab, layout);
    }

  bool any_stub_table_changed = false;
  for (Stub_table_iterator siter = this->stub_tables_.begin();
       siter != this->stub_tables_.end() && !any_stub_table_changed; ++siter)
    {
      The_stub_table* stub_table = *siter;
      if (stub_table->update_data_size_changed_p())
	{
	  The_aarch64_input_section* owner = stub_table->owner();
	  uint64_t address = owner->address();
	  off_t offset = owner->offset();
	  owner->reset_address_and_file_offset();
	  owner->set_address_and_file_offset(address, offset);

	  any_stub_table_changed = true;
	}
    }

  // Do not continue relaxation.
  bool continue_relaxation = any_stub_table_changed;
  if (!continue_relaxation)
    for (Stub_table_iterator sp = this->stub_tables_.begin();
	 (sp != this->stub_tables_.end());
	 ++sp)
      (*sp)->finalize_stubs();

  return continue_relaxation;
}


// Make a new Stub_table.

template<int size, bool big_endian>
Stub_table<size, big_endian>*
Target_aarch64<size, big_endian>::new_stub_table(
    AArch64_input_section<size, big_endian>* owner)
{
  Stub_table<size, big_endian>* stub_table =
      new Stub_table<size, big_endian>(owner);
  stub_table->set_address(align_address(
      owner->address() + owner->data_size(), 8));
  stub_table->set_file_offset(owner->offset() + owner->data_size());
  stub_table->finalize_data_size();

  this->stub_tables_.push_back(stub_table);

  return stub_table;
}


template<int size, bool big_endian>
typename elfcpp::Elf_types<size>::Elf_Addr
Target_aarch64<size, big_endian>::do_reloc_addend(
    void* arg, unsigned int r_type,
    typename elfcpp::Elf_types<size>::Elf_Addr) const
{
  gold_assert(r_type == elfcpp::R_AARCH64_TLSDESC);
  uintptr_t intarg = reinterpret_cast<uintptr_t>(arg);
  gold_assert(intarg < this->tlsdesc_reloc_info_.size());
  const Tlsdesc_info& ti(this->tlsdesc_reloc_info_[intarg]);
  const Symbol_value<size>* psymval = ti.object->local_symbol(ti.r_sym);
  gold_assert(psymval->is_tls_symbol());
  // The value of a TLS symbol is the offset in the TLS segment.
  return psymval->value(ti.object, 0);
}

// Return the number of entries in the PLT.

template<int size, bool big_endian>
unsigned int
Target_aarch64<size, big_endian>::plt_entry_count() const
{
  if (this->plt_ == NULL)
    return 0;
  return this->plt_->entry_count();
}

// Return the offset of the first non-reserved PLT entry.

template<int size, bool big_endian>
unsigned int
Target_aarch64<size, big_endian>::first_plt_entry_offset() const
{
  return this->plt_->first_plt_entry_offset();
}

// Return the size of each PLT entry.

template<int size, bool big_endian>
unsigned int
Target_aarch64<size, big_endian>::plt_entry_size() const
{
  return this->plt_->get_plt_entry_size();
}

// Define the _TLS_MODULE_BASE_ symbol in the TLS segment.

template<int size, bool big_endian>
void
Target_aarch64<size, big_endian>::define_tls_base_symbol(
    Symbol_table* symtab, Layout* layout)
{
  if (this->tls_base_symbol_defined_)
    return;

  Output_segment* tls_segment = layout->tls_segment();
  if (tls_segment != NULL)
    {
      bool is_exec = parameters->options().output_is_executable();
      symtab->define_in_output_segment("_TLS_MODULE_BASE_", NULL,
				       Symbol_table::PREDEFINED,
				       tls_segment, 0, 0,
				       elfcpp::STT_TLS,
				       elfcpp::STB_LOCAL,
				       elfcpp::STV_HIDDEN, 0,
				       (is_exec
					? Symbol::SEGMENT_END
					: Symbol::SEGMENT_START),
				       true);
    }
  this->tls_base_symbol_defined_ = true;
}

// Create the reserved PLT and GOT entries for the TLS descriptor resolver.

template<int size, bool big_endian>
void
Target_aarch64<size, big_endian>::reserve_tlsdesc_entries(
    Symbol_table* symtab, Layout* layout)
{
  if (this->plt_ == NULL)
    this->make_plt_section(symtab, layout);

  if (!this->plt_->has_tlsdesc_entry())
    {
      // Allocate the TLSDESC_GOT entry.
      Output_data_got_aarch64<size, big_endian>* got =
	  this->got_section(symtab, layout);
      unsigned int got_offset = got->add_constant(0);

      // Allocate the TLSDESC_PLT entry.
      this->plt_->reserve_tlsdesc_entry(got_offset);
    }
}

// Create a GOT entry for the TLS module index.

template<int size, bool big_endian>
unsigned int
Target_aarch64<size, big_endian>::got_mod_index_entry(
    Symbol_table* symtab, Layout* layout,
    Sized_relobj_file<size, big_endian>* object)
{
  if (this->got_mod_index_offset_ == -1U)
    {
      gold_assert(symtab != NULL && layout != NULL && object != NULL);
      Reloc_section* rela_dyn = this->rela_dyn_section(layout);
      Output_data_got_aarch64<size, big_endian>* got =
	  this->got_section(symtab, layout);
      unsigned int got_offset = got->add_constant(0);
      rela_dyn->add_local(object, 0, elfcpp::R_AARCH64_TLS_DTPMOD64, got,
			  got_offset, 0);
      got->add_constant(0);
      this->got_mod_index_offset_ = got_offset;
    }
  return this->got_mod_index_offset_;
}

// Optimize the TLS relocation type based on what we know about the
// symbol.  IS_FINAL is true if the final address of this symbol is
// known at link time.

template<int size, bool big_endian>
tls::Tls_optimization
Target_aarch64<size, big_endian>::optimize_tls_reloc(bool is_final,
						     int r_type)
{
  // If we are generating a shared library, then we can't do anything
  // in the linker
  if (parameters->options().shared())
    return tls::TLSOPT_NONE;

  switch (r_type)
    {
    case elfcpp::R_AARCH64_TLSGD_ADR_PAGE21:
    case elfcpp::R_AARCH64_TLSGD_ADD_LO12_NC:
    case elfcpp::R_AARCH64_TLSDESC_LD_PREL19:
    case elfcpp::R_AARCH64_TLSDESC_ADR_PREL21:
    case elfcpp::R_AARCH64_TLSDESC_ADR_PAGE21:
    case elfcpp::R_AARCH64_TLSDESC_LD64_LO12:
    case elfcpp::R_AARCH64_TLSDESC_ADD_LO12:
    case elfcpp::R_AARCH64_TLSDESC_OFF_G1:
    case elfcpp::R_AARCH64_TLSDESC_OFF_G0_NC:
    case elfcpp::R_AARCH64_TLSDESC_LDR:
    case elfcpp::R_AARCH64_TLSDESC_ADD:
    case elfcpp::R_AARCH64_TLSDESC_CALL:
      // These are General-Dynamic which permits fully general TLS
      // access.  Since we know that we are generating an executable,
      // we can convert this to Initial-Exec.  If we also know that
      // this is a local symbol, we can further switch to Local-Exec.
      if (is_final)
	return tls::TLSOPT_TO_LE;
      return tls::TLSOPT_TO_IE;

    case elfcpp::R_AARCH64_TLSLD_ADR_PAGE21:
    case elfcpp::R_AARCH64_TLSLD_ADD_LO12_NC:
    case elfcpp::R_AARCH64_TLSLD_MOVW_DTPREL_G1:
    case elfcpp::R_AARCH64_TLSLD_MOVW_DTPREL_G0_NC:
      // These are Local-Dynamic, which refer to local symbols in the
      // dynamic TLS block. Since we know that we generating an
      // executable, we can switch to Local-Exec.
      return tls::TLSOPT_TO_LE;

    case elfcpp::R_AARCH64_TLSIE_MOVW_GOTTPREL_G1:
    case elfcpp::R_AARCH64_TLSIE_MOVW_GOTTPREL_G0_NC:
    case elfcpp::R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
    case elfcpp::R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
    case elfcpp::R_AARCH64_TLSIE_LD_GOTTPREL_PREL19:
      // These are Initial-Exec relocs which get the thread offset
      // from the GOT. If we know that we are linking against the
      // local symbol, we can switch to Local-Exec, which links the
      // thread offset into the instruction.
      if (is_final)
	return tls::TLSOPT_TO_LE;
      return tls::TLSOPT_NONE;

    case elfcpp::R_AARCH64_TLSLE_MOVW_TPREL_G2:
    case elfcpp::R_AARCH64_TLSLE_MOVW_TPREL_G1:
    case elfcpp::R_AARCH64_TLSLE_MOVW_TPREL_G1_NC:
    case elfcpp::R_AARCH64_TLSLE_MOVW_TPREL_G0:
    case elfcpp::R_AARCH64_TLSLE_MOVW_TPREL_G0_NC:
    case elfcpp::R_AARCH64_TLSLE_ADD_TPREL_HI12:
    case elfcpp::R_AARCH64_TLSLE_ADD_TPREL_LO12:
    case elfcpp::R_AARCH64_TLSLE_ADD_TPREL_LO12_NC:
      // When we already have Local-Exec, there is nothing further we
      // can do.
      return tls::TLSOPT_NONE;

    default:
      gold_unreachable();
    }
}

// Returns true if this relocation type could be that of a function pointer.

template<int size, bool big_endian>
inline bool
Target_aarch64<size, big_endian>::Scan::possible_function_pointer_reloc(
  unsigned int r_type)
{
  switch (r_type)
    {
    case elfcpp::R_AARCH64_ADR_PREL_PG_HI21:
    case elfcpp::R_AARCH64_ADR_PREL_PG_HI21_NC:
    case elfcpp::R_AARCH64_ADD_ABS_LO12_NC:
    case elfcpp::R_AARCH64_ADR_GOT_PAGE:
    case elfcpp::R_AARCH64_LD64_GOT_LO12_NC:
      {
	return true;
      }
    }
  return false;
}

// For safe ICF, scan a relocation for a local symbol to check if it
// corresponds to a function pointer being taken.  In that case mark
// the function whose pointer was taken as not foldable.

template<int size, bool big_endian>
inline bool
Target_aarch64<size, big_endian>::Scan::local_reloc_may_be_function_pointer(
  Symbol_table* ,
  Layout* ,
  Target_aarch64<size, big_endian>* ,
  Sized_relobj_file<size, big_endian>* ,
  unsigned int ,
  Output_section* ,
  const elfcpp::Rela<size, big_endian>& ,
  unsigned int r_type,
  const elfcpp::Sym<size, big_endian>&)
{
  // When building a shared library, do not fold any local symbols.
  return (parameters->options().shared()
	  || possible_function_pointer_reloc(r_type));
}

// For safe ICF, scan a relocation for a global symbol to check if it
// corresponds to a function pointer being taken.  In that case mark
// the function whose pointer was taken as not foldable.

template<int size, bool big_endian>
inline bool
Target_aarch64<size, big_endian>::Scan::global_reloc_may_be_function_pointer(
  Symbol_table* ,
  Layout* ,
  Target_aarch64<size, big_endian>* ,
  Sized_relobj_file<size, big_endian>* ,
  unsigned int ,
  Output_section* ,
  const elfcpp::Rela<size, big_endian>& ,
  unsigned int r_type,
  Symbol* gsym)
{
  // When building a shared library, do not fold symbols whose visibility
  // is hidden, internal or protected.
  return ((parameters->options().shared()
	   && (gsym->visibility() == elfcpp::STV_INTERNAL
	       || gsym->visibility() == elfcpp::STV_PROTECTED
	       || gsym->visibility() == elfcpp::STV_HIDDEN))
	  || possible_function_pointer_reloc(r_type));
}

// Report an unsupported relocation against a local symbol.

template<int size, bool big_endian>
void
Target_aarch64<size, big_endian>::Scan::unsupported_reloc_local(
     Sized_relobj_file<size, big_endian>* object,
     unsigned int r_type)
{
  gold_error(_("%s: unsupported reloc %u against local symbol"),
	     object->name().c_str(), r_type);
}

// We are about to emit a dynamic relocation of type R_TYPE.  If the
// dynamic linker does not support it, issue an error.

template<int size, bool big_endian>
void
Target_aarch64<size, big_endian>::Scan::check_non_pic(Relobj* object,
						      unsigned int r_type)
{
  gold_assert(r_type != elfcpp::R_AARCH64_NONE);

  switch (r_type)
    {
    // These are the relocation types supported by glibc for AARCH64.
    case elfcpp::R_AARCH64_NONE:
    case elfcpp::R_AARCH64_COPY:
    case elfcpp::R_AARCH64_GLOB_DAT:
    case elfcpp::R_AARCH64_JUMP_SLOT:
    case elfcpp::R_AARCH64_RELATIVE:
    case elfcpp::R_AARCH64_TLS_DTPREL64:
    case elfcpp::R_AARCH64_TLS_DTPMOD64:
    case elfcpp::R_AARCH64_TLS_TPREL64:
    case elfcpp::R_AARCH64_TLSDESC:
    case elfcpp::R_AARCH64_IRELATIVE:
    case elfcpp::R_AARCH64_ABS32:
    case elfcpp::R_AARCH64_ABS64:
      return;

    default:
      break;
    }

  // This prevents us from issuing more than one error per reloc
  // section. But we can still wind up issuing more than one
  // error per object file.
  if (this->issued_non_pic_error_)
    return;
  gold_assert(parameters->options().output_is_position_independent());
  object->error(_("requires unsupported dynamic reloc; "
		  "recompile with -fPIC"));
  this->issued_non_pic_error_ = true;
  return;
}

// Return whether we need to make a PLT entry for a relocation of the
// given type against a STT_GNU_IFUNC symbol.

template<int size, bool big_endian>
bool
Target_aarch64<size, big_endian>::Scan::reloc_needs_plt_for_ifunc(
    Sized_relobj_file<size, big_endian>* object,
    unsigned int r_type)
{
  const AArch64_reloc_property* arp =
      aarch64_reloc_property_table->get_reloc_property(r_type);
  gold_assert(arp != NULL);

  int flags = arp->reference_flags();
  if (flags & Symbol::TLS_REF)
    {
      gold_error(_("%s: unsupported TLS reloc %s for IFUNC symbol"),
		 object->name().c_str(), arp->name().c_str());
      return false;
    }
  return flags != 0;
}

// Scan a relocation for a local symbol.

template<int size, bool big_endian>
inline void
Target_aarch64<size, big_endian>::Scan::local(
    Symbol_table* symtab,
    Layout* layout,
    Target_aarch64<size, big_endian>* target,
    Sized_relobj_file<size, big_endian>* object,
    unsigned int data_shndx,
    Output_section* output_section,
    const elfcpp::Rela<size, big_endian>& rela,
    unsigned int r_type,
    const elfcpp::Sym<size, big_endian>& lsym,
    bool is_discarded)
{
  if (is_discarded)
    return;

  typedef Output_data_reloc<elfcpp::SHT_RELA, true, size, big_endian>
      Reloc_section;
  Output_data_got_aarch64<size, big_endian>* got =
      target->got_section(symtab, layout);
  unsigned int r_sym = elfcpp::elf_r_sym<size>(rela.get_r_info());

  // A local STT_GNU_IFUNC symbol may require a PLT entry.
  bool is_ifunc = lsym.get_st_type() == elfcpp::STT_GNU_IFUNC;
  if (is_ifunc && this->reloc_needs_plt_for_ifunc(object, r_type))
    target->make_local_ifunc_plt_entry(symtab, layout, object, r_sym);

  switch (r_type)
    {
    case elfcpp::R_AARCH64_ABS32:
    case elfcpp::R_AARCH64_ABS16:
      if (parameters->options().output_is_position_independent())
	{
	  gold_error(_("%s: unsupported reloc %u in pos independent link."),
		     object->name().c_str(), r_type);
	}
      break;

    case elfcpp::R_AARCH64_ABS64:
      // If building a shared library or pie, we need to mark this as a dynmic
      // reloction, so that the dynamic loader can relocate it.
      if (parameters->options().output_is_position_independent())
	{
	  Reloc_section* rela_dyn = target->rela_dyn_section(layout);
	  rela_dyn->add_local_relative(object, r_sym,
				       elfcpp::R_AARCH64_RELATIVE,
				       output_section,
				       data_shndx,
				       rela.get_r_offset(),
				       rela.get_r_addend(),
				       is_ifunc);
	}
      break;

    case elfcpp::R_AARCH64_PREL64:
    case elfcpp::R_AARCH64_PREL32:
    case elfcpp::R_AARCH64_PREL16:
      break;

    case elfcpp::R_AARCH64_LD_PREL_LO19:        // 273
    case elfcpp::R_AARCH64_ADR_PREL_LO21:       // 274
    case elfcpp::R_AARCH64_ADR_PREL_PG_HI21:    // 275
    case elfcpp::R_AARCH64_ADR_PREL_PG_HI21_NC: // 276
    case elfcpp::R_AARCH64_ADD_ABS_LO12_NC:     // 277
    case elfcpp::R_AARCH64_LDST8_ABS_LO12_NC:   // 278
    case elfcpp::R_AARCH64_LDST16_ABS_LO12_NC:  // 284
    case elfcpp::R_AARCH64_LDST32_ABS_LO12_NC:  // 285
    case elfcpp::R_AARCH64_LDST64_ABS_LO12_NC:  // 286
    case elfcpp::R_AARCH64_LDST128_ABS_LO12_NC: // 299
       break;

    // Control flow, pc-relative. We don't need to do anything for a relative
    // addressing relocation against a local symbol if it does not reference
    // the GOT.
    case elfcpp::R_AARCH64_TSTBR14:
    case elfcpp::R_AARCH64_CONDBR19:
    case elfcpp::R_AARCH64_JUMP26:
    case elfcpp::R_AARCH64_CALL26:
      break;

    case elfcpp::R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
    case elfcpp::R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
      {
	tls::Tls_optimization tlsopt = Target_aarch64<size, big_endian>::
	  optimize_tls_reloc(!parameters->options().shared(), r_type);
	if (tlsopt == tls::TLSOPT_TO_LE)
	  break;

	layout->set_has_static_tls();
	// Create a GOT entry for the tp-relative offset.
	if (!parameters->doing_static_link())
	  {
	    got->add_local_with_rel(object, r_sym, GOT_TYPE_TLS_OFFSET,
				    target->rela_dyn_section(layout),
				    elfcpp::R_AARCH64_TLS_TPREL64);
	  }
	else if (!object->local_has_got_offset(r_sym,
					       GOT_TYPE_TLS_OFFSET))
	  {
	    got->add_local(object, r_sym, GOT_TYPE_TLS_OFFSET);
	    unsigned int got_offset =
		object->local_got_offset(r_sym, GOT_TYPE_TLS_OFFSET);
	    const elfcpp::Elf_Xword addend = rela.get_r_addend();
	    gold_assert(addend == 0);
	    got->add_static_reloc(got_offset, elfcpp::R_AARCH64_TLS_TPREL64,
				  object, r_sym);
	  }
      }
      break;

    case elfcpp::R_AARCH64_TLSGD_ADR_PAGE21:
    case elfcpp::R_AARCH64_TLSGD_ADD_LO12_NC:
      {
	tls::Tls_optimization tlsopt = Target_aarch64<size, big_endian>::
	    optimize_tls_reloc(!parameters->options().shared(), r_type);
	if (tlsopt == tls::TLSOPT_TO_LE)
	  {
	    layout->set_has_static_tls();
	    break;
	  }
	gold_assert(tlsopt == tls::TLSOPT_NONE);

	got->add_local_pair_with_rel(object,r_sym, data_shndx,
				     GOT_TYPE_TLS_PAIR,
				     target->rela_dyn_section(layout),
				     elfcpp::R_AARCH64_TLS_DTPMOD64);
      }
      break;

    case elfcpp::R_AARCH64_TLSLE_ADD_TPREL_HI12:
    case elfcpp::R_AARCH64_TLSLE_ADD_TPREL_LO12:
    case elfcpp::R_AARCH64_TLSLE_ADD_TPREL_LO12_NC:
      {
	layout->set_has_static_tls();
	bool output_is_shared = parameters->options().shared();
	if (output_is_shared)
	  gold_error(_("%s: unsupported TLSLE reloc %u in shared code."),
		     object->name().c_str(), r_type);
      }
      break;

    case elfcpp::R_AARCH64_TLSLD_ADR_PAGE21:
    case elfcpp::R_AARCH64_TLSLD_ADD_LO12_NC:
      {
	tls::Tls_optimization tlsopt = Target_aarch64<size, big_endian>::
	    optimize_tls_reloc(!parameters->options().shared(), r_type);
	if (tlsopt == tls::TLSOPT_NONE)
	  {
	    // Create a GOT entry for the module index.
	    target->got_mod_index_entry(symtab, layout, object);
	  }
	else if (tlsopt != tls::TLSOPT_TO_LE)
	  unsupported_reloc_local(object, r_type);
      }
      break;

    case elfcpp::R_AARCH64_TLSLD_MOVW_DTPREL_G1:
    case elfcpp::R_AARCH64_TLSLD_MOVW_DTPREL_G0_NC:
      break;

    case elfcpp::R_AARCH64_TLSDESC_ADR_PAGE21:
    case elfcpp::R_AARCH64_TLSDESC_LD64_LO12:
    case elfcpp::R_AARCH64_TLSDESC_ADD_LO12:
      {
	tls::Tls_optimization tlsopt = Target_aarch64<size, big_endian>::
	    optimize_tls_reloc(!parameters->options().shared(), r_type);
	target->define_tls_base_symbol(symtab, layout);
	if (tlsopt == tls::TLSOPT_NONE)
	  {
	    // Create reserved PLT and GOT entries for the resolver.
	    target->reserve_tlsdesc_entries(symtab, layout);

	    // Generate a double GOT entry with an R_AARCH64_TLSDESC reloc.
	    // The R_AARCH64_TLSDESC reloc is resolved lazily, so the GOT
	    // entry needs to be in an area in .got.plt, not .got. Call
	    // got_section to make sure the section has been created.
	    target->got_section(symtab, layout);
	    Output_data_got<size, big_endian>* got =
		target->got_tlsdesc_section();
	    unsigned int r_sym = elfcpp::elf_r_sym<size>(rela.get_r_info());
	    if (!object->local_has_got_offset(r_sym, GOT_TYPE_TLS_DESC))
	      {
		unsigned int got_offset = got->add_constant(0);
		got->add_constant(0);
		object->set_local_got_offset(r_sym, GOT_TYPE_TLS_DESC,
					     got_offset);
		Reloc_section* rt = target->rela_tlsdesc_section(layout);
		// We store the arguments we need in a vector, and use
		// the index into the vector as the parameter to pass
		// to the target specific routines.
		uintptr_t intarg = target->add_tlsdesc_info(object, r_sym);
		void* arg = reinterpret_cast<void*>(intarg);
		rt->add_target_specific(elfcpp::R_AARCH64_TLSDESC, arg,
					got, got_offset, 0);
	      }
	  }
	else if (tlsopt != tls::TLSOPT_TO_LE)
	  unsupported_reloc_local(object, r_type);
      }
      break;

    case elfcpp::R_AARCH64_TLSDESC_CALL:
      break;

    default:
      unsupported_reloc_local(object, r_type);
    }
}


// Report an unsupported relocation against a global symbol.

template<int size, bool big_endian>
void
Target_aarch64<size, big_endian>::Scan::unsupported_reloc_global(
    Sized_relobj_file<size, big_endian>* object,
    unsigned int r_type,
    Symbol* gsym)
{
  gold_error(_("%s: unsupported reloc %u against global symbol %s"),
	     object->name().c_str(), r_type, gsym->demangled_name().c_str());
}

template<int size, bool big_endian>
inline void
Target_aarch64<size, big_endian>::Scan::global(
    Symbol_table* symtab,
    Layout* layout,
    Target_aarch64<size, big_endian>* target,
    Sized_relobj_file<size, big_endian> * object,
    unsigned int data_shndx,
    Output_section* output_section,
    const elfcpp::Rela<size, big_endian>& rela,
    unsigned int r_type,
    Symbol* gsym)
{
  // A STT_GNU_IFUNC symbol may require a PLT entry.
  if (gsym->type() == elfcpp::STT_GNU_IFUNC
      && this->reloc_needs_plt_for_ifunc(object, r_type))
    target->make_plt_entry(symtab, layout, gsym);

  typedef Output_data_reloc<elfcpp::SHT_RELA, true, size, big_endian>
    Reloc_section;
  const AArch64_reloc_property* arp =
      aarch64_reloc_property_table->get_reloc_property(r_type);
  gold_assert(arp != NULL);

  switch (r_type)
    {
    case elfcpp::R_AARCH64_ABS16:
    case elfcpp::R_AARCH64_ABS32:
    case elfcpp::R_AARCH64_ABS64:
      {
	// Make a PLT entry if necessary.
	if (gsym->needs_plt_entry())
	  {
	    target->make_plt_entry(symtab, layout, gsym);
	    // Since this is not a PC-relative relocation, we may be
	    // taking the address of a function. In that case we need to
	    // set the entry in the dynamic symbol table to the address of
	    // the PLT entry.
	    if (gsym->is_from_dynobj() && !parameters->options().shared())
	      gsym->set_needs_dynsym_value();
	  }
	// Make a dynamic relocation if necessary.
	if (gsym->needs_dynamic_reloc(arp->reference_flags()))
	  {
	    if (!parameters->options().output_is_position_independent()
		&& gsym->may_need_copy_reloc())
	      {
		target->copy_reloc(symtab, layout, object,
				   data_shndx, output_section, gsym, rela);
	      }
	    else if (r_type == elfcpp::R_AARCH64_ABS64
		     && gsym->type() == elfcpp::STT_GNU_IFUNC
		     && gsym->can_use_relative_reloc(false)
		     && !gsym->is_from_dynobj()
		     && !gsym->is_undefined()
		     && !gsym->is_preemptible())
	      {
		// Use an IRELATIVE reloc for a locally defined STT_GNU_IFUNC
		// symbol. This makes a function address in a PIE executable
		// match the address in a shared library that it links against.
		Reloc_section* rela_dyn =
		    target->rela_irelative_section(layout);
		unsigned int r_type = elfcpp::R_AARCH64_IRELATIVE;
		rela_dyn->add_symbolless_global_addend(gsym, r_type,
						       output_section, object,
						       data_shndx,
						       rela.get_r_offset(),
						       rela.get_r_addend());
	      }
	    else if (r_type == elfcpp::R_AARCH64_ABS64
		     && gsym->can_use_relative_reloc(false))
	      {
		Reloc_section* rela_dyn = target->rela_dyn_section(layout);
		rela_dyn->add_global_relative(gsym,
					      elfcpp::R_AARCH64_RELATIVE,
					      output_section,
					      object,
					      data_shndx,
					      rela.get_r_offset(),
					      rela.get_r_addend(),
					      false);
	      }
	    else
	      {
		check_non_pic(object, r_type);
		Output_data_reloc<elfcpp::SHT_RELA, true, size, big_endian>*
		    rela_dyn = target->rela_dyn_section(layout);
		rela_dyn->add_global(
		  gsym, r_type, output_section, object,
		  data_shndx, rela.get_r_offset(),rela.get_r_addend());
	      }
	  }
      }
      break;

    case elfcpp::R_AARCH64_PREL16:
    case elfcpp::R_AARCH64_PREL32:
    case elfcpp::R_AARCH64_PREL64:
      // This is used to fill the GOT absolute address.
      if (gsym->needs_plt_entry())
	{
	  target->make_plt_entry(symtab, layout, gsym);
	}
      break;

    case elfcpp::R_AARCH64_LD_PREL_LO19:        // 273
    case elfcpp::R_AARCH64_ADR_PREL_LO21:       // 274
    case elfcpp::R_AARCH64_ADR_PREL_PG_HI21:    // 275
    case elfcpp::R_AARCH64_ADR_PREL_PG_HI21_NC: // 276
    case elfcpp::R_AARCH64_ADD_ABS_LO12_NC:     // 277
    case elfcpp::R_AARCH64_LDST8_ABS_LO12_NC:   // 278
    case elfcpp::R_AARCH64_LDST16_ABS_LO12_NC:  // 284
    case elfcpp::R_AARCH64_LDST32_ABS_LO12_NC:  // 285
    case elfcpp::R_AARCH64_LDST64_ABS_LO12_NC:  // 286
    case elfcpp::R_AARCH64_LDST128_ABS_LO12_NC: // 299
      {
	if (gsym->needs_plt_entry())
	  target->make_plt_entry(symtab, layout, gsym);
	// Make a dynamic relocation if necessary.
	if (gsym->needs_dynamic_reloc(arp->reference_flags()))
	  {
	    if (parameters->options().output_is_executable()
		&& gsym->may_need_copy_reloc())
	      {
		target->copy_reloc(symtab, layout, object,
				   data_shndx, output_section, gsym, rela);
	      }
	  }
	break;
      }

    case elfcpp::R_AARCH64_ADR_GOT_PAGE:
    case elfcpp::R_AARCH64_LD64_GOT_LO12_NC:
      {
	// This pair of relocations is used to access a specific GOT entry.
	// Note a GOT entry is an *address* to a symbol.
	// The symbol requires a GOT entry
	Output_data_got_aarch64<size, big_endian>* got =
	  target->got_section(symtab, layout);
	if (gsym->final_value_is_known())
	  {
	    // For a STT_GNU_IFUNC symbol we want the PLT address.
	    if (gsym->type() == elfcpp::STT_GNU_IFUNC)
	      got->add_global_plt(gsym, GOT_TYPE_STANDARD);
	    else
	      got->add_global(gsym, GOT_TYPE_STANDARD);
	  }
	else
	  {
	    // If this symbol is not fully resolved, we need to add a dynamic
	    // relocation for it.
	    Reloc_section* rela_dyn = target->rela_dyn_section(layout);

	    // Use a GLOB_DAT rather than a RELATIVE reloc if:
	    //
	    // 1) The symbol may be defined in some other module.
	    // 2) We are building a shared library and this is a protected
	    // symbol; using GLOB_DAT means that the dynamic linker can use
	    // the address of the PLT in the main executable when appropriate
	    // so that function address comparisons work.
	    // 3) This is a STT_GNU_IFUNC symbol in position dependent code,
	    // again so that function address comparisons work.
	    if (gsym->is_from_dynobj()
		|| gsym->is_undefined()
		|| gsym->is_preemptible()
		|| (gsym->visibility() == elfcpp::STV_PROTECTED
		    && parameters->options().shared())
		|| (gsym->type() == elfcpp::STT_GNU_IFUNC
		    && parameters->options().output_is_position_independent()))
	      got->add_global_with_rel(gsym, GOT_TYPE_STANDARD,
				       rela_dyn, elfcpp::R_AARCH64_GLOB_DAT);
	    else
	      {
		// For a STT_GNU_IFUNC symbol we want to write the PLT
		// offset into the GOT, so that function pointer
		// comparisons work correctly.
		bool is_new;
		if (gsym->type() != elfcpp::STT_GNU_IFUNC)
		  is_new = got->add_global(gsym, GOT_TYPE_STANDARD);
		else
		  {
		    is_new = got->add_global_plt(gsym, GOT_TYPE_STANDARD);
		    // Tell the dynamic linker to use the PLT address
		    // when resolving relocations.
		    if (gsym->is_from_dynobj()
			&& !parameters->options().shared())
		      gsym->set_needs_dynsym_value();
		  }
		if (is_new)
		  {
		    rela_dyn->add_global_relative(
			gsym, elfcpp::R_AARCH64_RELATIVE,
			got,
			gsym->got_offset(GOT_TYPE_STANDARD),
			0,
			false);
		  }
	      }
	  }
	break;
      }

    case elfcpp::R_AARCH64_TSTBR14:
    case elfcpp::R_AARCH64_CONDBR19:
    case elfcpp::R_AARCH64_JUMP26:
    case elfcpp::R_AARCH64_CALL26:
      {
	if (gsym->final_value_is_known())
	  break;

	if (gsym->is_defined() &&
	    !gsym->is_from_dynobj() &&
	    !gsym->is_preemptible())
	  break;

	// Make plt entry for function call.
	target->make_plt_entry(symtab, layout, gsym);
	break;
      }

    case elfcpp::R_AARCH64_TLSGD_ADR_PAGE21:
    case elfcpp::R_AARCH64_TLSGD_ADD_LO12_NC:  // General dynamic
      {
	tls::Tls_optimization tlsopt = Target_aarch64<size, big_endian>::
	    optimize_tls_reloc(gsym->final_value_is_known(), r_type);
	if (tlsopt == tls::TLSOPT_TO_LE)
	  {
	    layout->set_has_static_tls();
	    break;
	  }
	gold_assert(tlsopt == tls::TLSOPT_NONE);

	// General dynamic.
	Output_data_got_aarch64<size, big_endian>* got =
	    target->got_section(symtab, layout);
	// Create 2 consecutive entries for module index and offset.
	got->add_global_pair_with_rel(gsym, GOT_TYPE_TLS_PAIR,
				      target->rela_dyn_section(layout),
				      elfcpp::R_AARCH64_TLS_DTPMOD64,
				      elfcpp::R_AARCH64_TLS_DTPREL64);
      }
      break;

    case elfcpp::R_AARCH64_TLSLD_ADR_PAGE21:
    case elfcpp::R_AARCH64_TLSLD_ADD_LO12_NC:  // Local dynamic
      {
	tls::Tls_optimization tlsopt = Target_aarch64<size, big_endian>::
	    optimize_tls_reloc(!parameters->options().shared(), r_type);
	if (tlsopt == tls::TLSOPT_NONE)
	  {
	    // Create a GOT entry for the module index.
	    target->got_mod_index_entry(symtab, layout, object);
	  }
	else if (tlsopt != tls::TLSOPT_TO_LE)
	  unsupported_reloc_local(object, r_type);
      }
      break;

    case elfcpp::R_AARCH64_TLSLD_MOVW_DTPREL_G1:
    case elfcpp::R_AARCH64_TLSLD_MOVW_DTPREL_G0_NC:  // Other local dynamic
      break;

    case elfcpp::R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
    case elfcpp::R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:  // Initial executable
      {
	tls::Tls_optimization tlsopt = Target_aarch64<size, big_endian>::
	  optimize_tls_reloc(gsym->final_value_is_known(), r_type);
	if (tlsopt == tls::TLSOPT_TO_LE)
	  break;

	layout->set_has_static_tls();
	// Create a GOT entry for the tp-relative offset.
	Output_data_got_aarch64<size, big_endian>* got
	  = target->got_section(symtab, layout);
	if (!parameters->doing_static_link())
	  {
	    got->add_global_with_rel(
	      gsym, GOT_TYPE_TLS_OFFSET,
	      target->rela_dyn_section(layout),
	      elfcpp::R_AARCH64_TLS_TPREL64);
	  }
	if (!gsym->has_got_offset(GOT_TYPE_TLS_OFFSET))
	  {
	    got->add_global(gsym, GOT_TYPE_TLS_OFFSET);
	    unsigned int got_offset =
	      gsym->got_offset(GOT_TYPE_TLS_OFFSET);
	    const elfcpp::Elf_Xword addend = rela.get_r_addend();
	    gold_assert(addend == 0);
	    got->add_static_reloc(got_offset,
				  elfcpp::R_AARCH64_TLS_TPREL64, gsym);
	  }
      }
      break;

    case elfcpp::R_AARCH64_TLSLE_ADD_TPREL_HI12:
    case elfcpp::R_AARCH64_TLSLE_ADD_TPREL_LO12:
    case elfcpp::R_AARCH64_TLSLE_ADD_TPREL_LO12_NC:  // Local executable
      layout->set_has_static_tls();
      if (parameters->options().shared())
	gold_error(_("%s: unsupported TLSLE reloc type %u in shared objects."),
		   object->name().c_str(), r_type);
      break;

    case elfcpp::R_AARCH64_TLSDESC_ADR_PAGE21:
    case elfcpp::R_AARCH64_TLSDESC_LD64_LO12:
    case elfcpp::R_AARCH64_TLSDESC_ADD_LO12:  // TLS descriptor
      {
	target->define_tls_base_symbol(symtab, layout);
	tls::Tls_optimization tlsopt = Target_aarch64<size, big_endian>::
	    optimize_tls_reloc(gsym->final_value_is_known(), r_type);
	if (tlsopt == tls::TLSOPT_NONE)
	  {
	    // Create reserved PLT and GOT entries for the resolver.
	    target->reserve_tlsdesc_entries(symtab, layout);

	    // Create a double GOT entry with an R_AARCH64_TLSDESC
	    // relocation. The R_AARCH64_TLSDESC is resolved lazily, so the GOT
	    // entry needs to be in an area in .got.plt, not .got. Call
	    // got_section to make sure the section has been created.
	    target->got_section(symtab, layout);
	    Output_data_got<size, big_endian>* got =
		target->got_tlsdesc_section();
	    Reloc_section* rt = target->rela_tlsdesc_section(layout);
	    got->add_global_pair_with_rel(gsym, GOT_TYPE_TLS_DESC, rt,
					  elfcpp::R_AARCH64_TLSDESC, 0);
	  }
	else if (tlsopt == tls::TLSOPT_TO_IE)
	  {
	    // Create a GOT entry for the tp-relative offset.
	    Output_data_got<size, big_endian>* got
		= target->got_section(symtab, layout);
	    got->add_global_with_rel(gsym, GOT_TYPE_TLS_OFFSET,
				     target->rela_dyn_section(layout),
				     elfcpp::R_AARCH64_TLS_TPREL64);
	  }
	else if (tlsopt != tls::TLSOPT_TO_LE)
	  unsupported_reloc_global(object, r_type, gsym);
      }
      break;

    case elfcpp::R_AARCH64_TLSDESC_CALL:
      break;

    default:
      gold_error(_("%s: unsupported reloc type in global scan"),
		 aarch64_reloc_property_table->
		 reloc_name_in_error_message(r_type).c_str());
    }
  return;
}  // End of Scan::global


// Create the PLT section.
template<int size, bool big_endian>
void
Target_aarch64<size, big_endian>::make_plt_section(
  Symbol_table* symtab, Layout* layout)
{
  if (this->plt_ == NULL)
    {
      // Create the GOT section first.
      this->got_section(symtab, layout);

      this->plt_ = this->make_data_plt(layout, this->got_, this->got_plt_,
				       this->got_irelative_);

      layout->add_output_section_data(".plt", elfcpp::SHT_PROGBITS,
				      (elfcpp::SHF_ALLOC
				       | elfcpp::SHF_EXECINSTR),
				      this->plt_, ORDER_PLT, false);

      // Make the sh_info field of .rela.plt point to .plt.
      Output_section* rela_plt_os = this->plt_->rela_plt()->output_section();
      rela_plt_os->set_info_section(this->plt_->output_section());
    }
}

// Return the section for TLSDESC relocations.

template<int size, bool big_endian>
typename Target_aarch64<size, big_endian>::Reloc_section*
Target_aarch64<size, big_endian>::rela_tlsdesc_section(Layout* layout) const
{
  return this->plt_section()->rela_tlsdesc(layout);
}

// Create a PLT entry for a global symbol.

template<int size, bool big_endian>
void
Target_aarch64<size, big_endian>::make_plt_entry(
    Symbol_table* symtab,
    Layout* layout,
    Symbol* gsym)
{
  if (gsym->has_plt_offset())
    return;

  if (this->plt_ == NULL)
    this->make_plt_section(symtab, layout);

  this->plt_->add_entry(symtab, layout, gsym);
}

// Make a PLT entry for a local STT_GNU_IFUNC symbol.

template<int size, bool big_endian>
void
Target_aarch64<size, big_endian>::make_local_ifunc_plt_entry(
    Symbol_table* symtab, Layout* layout,
    Sized_relobj_file<size, big_endian>* relobj,
    unsigned int local_sym_index)
{
  if (relobj->local_has_plt_offset(local_sym_index))
    return;
  if (this->plt_ == NULL)
    this->make_plt_section(symtab, layout);
  unsigned int plt_offset = this->plt_->add_local_ifunc_entry(symtab, layout,
							      relobj,
							      local_sym_index);
  relobj->set_local_plt_offset(local_sym_index, plt_offset);
}

template<int size, bool big_endian>
void
Target_aarch64<size, big_endian>::gc_process_relocs(
    Symbol_table* symtab,
    Layout* layout,
    Sized_relobj_file<size, big_endian>* object,
    unsigned int data_shndx,
    unsigned int sh_type,
    const unsigned char* prelocs,
    size_t reloc_count,
    Output_section* output_section,
    bool needs_special_offset_handling,
    size_t local_symbol_count,
    const unsigned char* plocal_symbols)
{
  if (sh_type == elfcpp::SHT_REL)
    {
      return;
    }

  gold::gc_process_relocs<
    size, big_endian,
    Target_aarch64<size, big_endian>,
    elfcpp::SHT_RELA,
    typename Target_aarch64<size, big_endian>::Scan,
    typename Target_aarch64<size, big_endian>::Relocatable_size_for_reloc>(
    symtab,
    layout,
    this,
    object,
    data_shndx,
    prelocs,
    reloc_count,
    output_section,
    needs_special_offset_handling,
    local_symbol_count,
    plocal_symbols);
}

// Scan relocations for a section.

template<int size, bool big_endian>
void
Target_aarch64<size, big_endian>::scan_relocs(
    Symbol_table* symtab,
    Layout* layout,
    Sized_relobj_file<size, big_endian>* object,
    unsigned int data_shndx,
    unsigned int sh_type,
    const unsigned char* prelocs,
    size_t reloc_count,
    Output_section* output_section,
    bool needs_special_offset_handling,
    size_t local_symbol_count,
    const unsigned char* plocal_symbols)
{
  if (sh_type == elfcpp::SHT_REL)
    {
      gold_error(_("%s: unsupported REL reloc section"),
		 object->name().c_str());
      return;
    }
  gold::scan_relocs<size, big_endian, Target_aarch64, elfcpp::SHT_RELA, Scan>(
    symtab,
    layout,
    this,
    object,
    data_shndx,
    prelocs,
    reloc_count,
    output_section,
    needs_special_offset_handling,
    local_symbol_count,
    plocal_symbols);
}

// Return the value to use for a dynamic which requires special
// treatment.  This is how we support equality comparisons of function
// pointers across shared library boundaries, as described in the
// processor specific ABI supplement.

template<int size, bool big_endian>
uint64_t
Target_aarch64<size, big_endian>::do_dynsym_value(const Symbol* gsym) const
{
  gold_assert(gsym->is_from_dynobj() && gsym->has_plt_offset());
  return this->plt_address_for_global(gsym);
}


// Finalize the sections.

template<int size, bool big_endian>
void
Target_aarch64<size, big_endian>::do_finalize_sections(
    Layout* layout,
    const Input_objects*,
    Symbol_table* symtab)
{
  const Reloc_section* rel_plt = (this->plt_ == NULL
				  ? NULL
				  : this->plt_->rela_plt());
  layout->add_target_dynamic_tags(false, this->got_plt_, rel_plt,
				  this->rela_dyn_, true, false);

  // Emit any relocs we saved in an attempt to avoid generating COPY
  // relocs.
  if (this->copy_relocs_.any_saved_relocs())
    this->copy_relocs_.emit(this->rela_dyn_section(layout));

  // Fill in some more dynamic tags.
  Output_data_dynamic* const odyn = layout->dynamic_data();
  if (odyn != NULL)
    {
      if (this->plt_ != NULL
	  && this->plt_->output_section() != NULL
	  && this->plt_ ->has_tlsdesc_entry())
	{
	  unsigned int plt_offset = this->plt_->get_tlsdesc_plt_offset();
	  unsigned int got_offset = this->plt_->get_tlsdesc_got_offset();
	  this->got_->finalize_data_size();
	  odyn->add_section_plus_offset(elfcpp::DT_TLSDESC_PLT,
					this->plt_, plt_offset);
	  odyn->add_section_plus_offset(elfcpp::DT_TLSDESC_GOT,
					this->got_, got_offset);
	}
    }

  // Set the size of the _GLOBAL_OFFSET_TABLE_ symbol to the size of
  // the .got.plt section.
  Symbol* sym = this->global_offset_table_;
  if (sym != NULL)
    {
      uint64_t data_size = this->got_plt_->current_data_size();
      symtab->get_sized_symbol<size>(sym)->set_symsize(data_size);

      // If the .got section is more than 0x8000 bytes, we add
      // 0x8000 to the value of _GLOBAL_OFFSET_TABLE_, so that 16
      // bit relocations have a greater chance of working.
      if (data_size >= 0x8000)
	symtab->get_sized_symbol<size>(sym)->set_value(
	  symtab->get_sized_symbol<size>(sym)->value() + 0x8000);
    }

  if (parameters->doing_static_link()
      && (this->plt_ == NULL || !this->plt_->has_irelative_section()))
    {
      // If linking statically, make sure that the __rela_iplt symbols
      // were defined if necessary, even if we didn't create a PLT.
      static const Define_symbol_in_segment syms[] =
	{
	  {
	    "__rela_iplt_start",	// name
	    elfcpp::PT_LOAD,		// segment_type
	    elfcpp::PF_W,		// segment_flags_set
	    elfcpp::PF(0),		// segment_flags_clear
	    0,				// value
	    0,				// size
	    elfcpp::STT_NOTYPE,		// type
	    elfcpp::STB_GLOBAL,		// binding
	    elfcpp::STV_HIDDEN,		// visibility
	    0,				// nonvis
	    Symbol::SEGMENT_START,	// offset_from_base
	    true			// only_if_ref
	  },
	  {
	    "__rela_iplt_end",		// name
	    elfcpp::PT_LOAD,		// segment_type
	    elfcpp::PF_W,		// segment_flags_set
	    elfcpp::PF(0),		// segment_flags_clear
	    0,				// value
	    0,				// size
	    elfcpp::STT_NOTYPE,		// type
	    elfcpp::STB_GLOBAL,		// binding
	    elfcpp::STV_HIDDEN,		// visibility
	    0,				// nonvis
	    Symbol::SEGMENT_START,	// offset_from_base
	    true			// only_if_ref
	  }
	};

      symtab->define_symbols(layout, 2, syms,
			     layout->script_options()->saw_sections_clause());
    }

  return;
}

// Perform a relocation.

template<int size, bool big_endian>
inline bool
Target_aarch64<size, big_endian>::Relocate::relocate(
    const Relocate_info<size, big_endian>* relinfo,
    Target_aarch64<size, big_endian>* target,
    Output_section* ,
    size_t relnum,
    const elfcpp::Rela<size, big_endian>& rela,
    unsigned int r_type,
    const Sized_symbol<size>* gsym,
    const Symbol_value<size>* psymval,
    unsigned char* view,
    typename elfcpp::Elf_types<size>::Elf_Addr address,
    section_size_type /* view_size */)
{
  if (view == NULL)
    return true;

  typedef AArch64_relocate_functions<size, big_endian> Reloc;

  const AArch64_reloc_property* reloc_property =
      aarch64_reloc_property_table->get_reloc_property(r_type);

  if (reloc_property == NULL)
    {
      std::string reloc_name =
	  aarch64_reloc_property_table->reloc_name_in_error_message(r_type);
      gold_error_at_location(relinfo, relnum, rela.get_r_offset(),
			     _("cannot relocate %s in object file"),
			     reloc_name.c_str());
      return true;
    }

  const Sized_relobj_file<size, big_endian>* object = relinfo->object;

  // Pick the value to use for symbols defined in the PLT.
  Symbol_value<size> symval;
  if (gsym != NULL
      && gsym->use_plt_offset(reloc_property->reference_flags()))
    {
      symval.set_output_value(target->plt_address_for_global(gsym));
      psymval = &symval;
    }
  else if (gsym == NULL && psymval->is_ifunc_symbol())
    {
      unsigned int r_sym = elfcpp::elf_r_sym<size>(rela.get_r_info());
      if (object->local_has_plt_offset(r_sym))
	{
	  symval.set_output_value(target->plt_address_for_local(object, r_sym));
	  psymval = &symval;
	}
    }

  const elfcpp::Elf_Xword addend = rela.get_r_addend();

  // Get the GOT offset if needed.
  // For aarch64, the GOT pointer points to the start of the GOT section.
  bool have_got_offset = false;
  int got_offset = 0;
  int got_base = (target->got_ != NULL
		  ? (target->got_->current_data_size() >= 0x8000
		     ? 0x8000 : 0)
		  : 0);
  switch (r_type)
    {
    case elfcpp::R_AARCH64_MOVW_GOTOFF_G0:
    case elfcpp::R_AARCH64_MOVW_GOTOFF_G0_NC:
    case elfcpp::R_AARCH64_MOVW_GOTOFF_G1:
    case elfcpp::R_AARCH64_MOVW_GOTOFF_G1_NC:
    case elfcpp::R_AARCH64_MOVW_GOTOFF_G2:
    case elfcpp::R_AARCH64_MOVW_GOTOFF_G2_NC:
    case elfcpp::R_AARCH64_MOVW_GOTOFF_G3:
    case elfcpp::R_AARCH64_GOTREL64:
    case elfcpp::R_AARCH64_GOTREL32:
    case elfcpp::R_AARCH64_GOT_LD_PREL19:
    case elfcpp::R_AARCH64_LD64_GOTOFF_LO15:
    case elfcpp::R_AARCH64_ADR_GOT_PAGE:
    case elfcpp::R_AARCH64_LD64_GOT_LO12_NC:
    case elfcpp::R_AARCH64_LD64_GOTPAGE_LO15:
      if (gsym != NULL)
	{
	  gold_assert(gsym->has_got_offset(GOT_TYPE_STANDARD));
	  got_offset = gsym->got_offset(GOT_TYPE_STANDARD) - got_base;
	}
      else
	{
	  unsigned int r_sym = elfcpp::elf_r_sym<size>(rela.get_r_info());
	  gold_assert(object->local_has_got_offset(r_sym, GOT_TYPE_STANDARD));
	  got_offset = (object->local_got_offset(r_sym, GOT_TYPE_STANDARD)
			- got_base);
	}
      have_got_offset = true;
      break;

    default:
      break;
    }

  typename Reloc::Status reloc_status = Reloc::STATUS_OKAY;
  typename elfcpp::Elf_types<size>::Elf_Addr value;
  switch (r_type)
    {
    case elfcpp::R_AARCH64_NONE:
      break;

    case elfcpp::R_AARCH64_ABS64:
      reloc_status = Reloc::template rela_ua<64>(
	view, object, psymval, addend, reloc_property);
      break;

    case elfcpp::R_AARCH64_ABS32:
      reloc_status = Reloc::template rela_ua<32>(
	view, object, psymval, addend, reloc_property);
      break;

    case elfcpp::R_AARCH64_ABS16:
      reloc_status = Reloc::template rela_ua<16>(
	view, object, psymval, addend, reloc_property);
      break;

    case elfcpp::R_AARCH64_PREL64:
      reloc_status = Reloc::template pcrela_ua<64>(
	view, object, psymval, addend, address, reloc_property);
      break;

    case elfcpp::R_AARCH64_PREL32:
      reloc_status = Reloc::template pcrela_ua<32>(
	view, object, psymval, addend, address, reloc_property);
      break;

    case elfcpp::R_AARCH64_PREL16:
      reloc_status = Reloc::template pcrela_ua<16>(
	view, object, psymval, addend, address, reloc_property);
      break;

    case elfcpp::R_AARCH64_LD_PREL_LO19:
      reloc_status = Reloc::template pcrela_general<32>(
	  view, object, psymval, addend, address, reloc_property);
      break;

    case elfcpp::R_AARCH64_ADR_PREL_LO21:
      reloc_status = Reloc::adr(view, object, psymval, addend,
				address, reloc_property);
      break;

    case elfcpp::R_AARCH64_ADR_PREL_PG_HI21_NC:
    case elfcpp::R_AARCH64_ADR_PREL_PG_HI21:
      reloc_status = Reloc::adrp(view, object, psymval, addend, address,
				 reloc_property);
      break;

    case elfcpp::R_AARCH64_LDST8_ABS_LO12_NC:
    case elfcpp::R_AARCH64_LDST16_ABS_LO12_NC:
    case elfcpp::R_AARCH64_LDST32_ABS_LO12_NC:
    case elfcpp::R_AARCH64_LDST64_ABS_LO12_NC:
    case elfcpp::R_AARCH64_LDST128_ABS_LO12_NC:
    case elfcpp::R_AARCH64_ADD_ABS_LO12_NC:
      reloc_status = Reloc::template rela_general<32>(
	view, object, psymval, addend, reloc_property);
      break;

    case elfcpp::R_AARCH64_CALL26:
      if (this->skip_call_tls_get_addr_)
	{
	  // Double check that the TLSGD insn has been optimized away.
	  typedef typename elfcpp::Swap<32, big_endian>::Valtype Insntype;
	  Insntype insn = elfcpp::Swap<32, big_endian>::readval(
	      reinterpret_cast<Insntype*>(view));
	  gold_assert((insn & 0xff000000) == 0x91000000);

	  reloc_status = Reloc::STATUS_OKAY;
	  this->skip_call_tls_get_addr_ = false;
	  // Return false to stop further processing this reloc.
	  return false;
	}
      // Fallthrough
    case elfcpp::R_AARCH64_JUMP26:
      if (Reloc::maybe_apply_stub(r_type, relinfo, rela, view, address,
				  gsym, psymval, object))
	break;
      // Fallthrough
    case elfcpp::R_AARCH64_TSTBR14:
    case elfcpp::R_AARCH64_CONDBR19:
      reloc_status = Reloc::template pcrela_general<32>(
	view, object, psymval, addend, address, reloc_property);
      break;

    case elfcpp::R_AARCH64_ADR_GOT_PAGE:
      gold_assert(have_got_offset);
      value = target->got_->address() + got_base + got_offset;
      reloc_status = Reloc::adrp(view, value + addend, address);
      break;

    case elfcpp::R_AARCH64_LD64_GOT_LO12_NC:
      gold_assert(have_got_offset);
      value = target->got_->address() + got_base + got_offset;
      reloc_status = Reloc::template rela_general<32>(
	view, value, addend, reloc_property);
      break;

    case elfcpp::R_AARCH64_TLSGD_ADR_PAGE21:
    case elfcpp::R_AARCH64_TLSGD_ADD_LO12_NC:
    case elfcpp::R_AARCH64_TLSLD_ADR_PAGE21:
    case elfcpp::R_AARCH64_TLSLD_ADD_LO12_NC:
    case elfcpp::R_AARCH64_TLSLD_MOVW_DTPREL_G1:
    case elfcpp::R_AARCH64_TLSLD_MOVW_DTPREL_G0_NC:
    case elfcpp::R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
    case elfcpp::R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
    case elfcpp::R_AARCH64_TLSLE_ADD_TPREL_HI12:
    case elfcpp::R_AARCH64_TLSLE_ADD_TPREL_LO12:
    case elfcpp::R_AARCH64_TLSLE_ADD_TPREL_LO12_NC:
    case elfcpp::R_AARCH64_TLSDESC_ADR_PAGE21:
    case elfcpp::R_AARCH64_TLSDESC_LD64_LO12:
    case elfcpp::R_AARCH64_TLSDESC_ADD_LO12:
    case elfcpp::R_AARCH64_TLSDESC_CALL:
      reloc_status = relocate_tls(relinfo, target, relnum, rela, r_type,
				  gsym, psymval, view, address);
      break;

    // These are dynamic relocations, which are unexpected when linking.
    case elfcpp::R_AARCH64_COPY:
    case elfcpp::R_AARCH64_GLOB_DAT:
    case elfcpp::R_AARCH64_JUMP_SLOT:
    case elfcpp::R_AARCH64_RELATIVE:
    case elfcpp::R_AARCH64_IRELATIVE:
    case elfcpp::R_AARCH64_TLS_DTPREL64:
    case elfcpp::R_AARCH64_TLS_DTPMOD64:
    case elfcpp::R_AARCH64_TLS_TPREL64:
    case elfcpp::R_AARCH64_TLSDESC:
      gold_error_at_location(relinfo, relnum, rela.get_r_offset(),
			     _("unexpected reloc %u in object file"),
			     r_type);
      break;

    default:
      gold_error_at_location(relinfo, relnum, rela.get_r_offset(),
			     _("unsupported reloc %s"),
			     reloc_property->name().c_str());
      break;
    }

  // Report any errors.
  switch (reloc_status)
    {
    case Reloc::STATUS_OKAY:
      break;
    case Reloc::STATUS_OVERFLOW:
      gold_error_at_location(relinfo, relnum, rela.get_r_offset(),
			     _("relocation overflow in %s"),
			     reloc_property->name().c_str());
      break;
    case Reloc::STATUS_BAD_RELOC:
      gold_error_at_location(
	  relinfo,
	  relnum,
	  rela.get_r_offset(),
	  _("unexpected opcode while processing relocation %s"),
	  reloc_property->name().c_str());
      break;
    default:
      gold_unreachable();
    }

  return true;
}


template<int size, bool big_endian>
inline
typename AArch64_relocate_functions<size, big_endian>::Status
Target_aarch64<size, big_endian>::Relocate::relocate_tls(
    const Relocate_info<size, big_endian>* relinfo,
    Target_aarch64<size, big_endian>* target,
    size_t relnum,
    const elfcpp::Rela<size, big_endian>& rela,
    unsigned int r_type, const Sized_symbol<size>* gsym,
    const Symbol_value<size>* psymval,
    unsigned char* view,
    typename elfcpp::Elf_types<size>::Elf_Addr address)
{
  typedef AArch64_relocate_functions<size, big_endian> aarch64_reloc_funcs;
  typedef typename elfcpp::Elf_types<size>::Elf_Addr AArch64_address;

  Output_segment* tls_segment = relinfo->layout->tls_segment();
  const elfcpp::Elf_Xword addend = rela.get_r_addend();
  const AArch64_reloc_property* reloc_property =
      aarch64_reloc_property_table->get_reloc_property(r_type);
  gold_assert(reloc_property != NULL);

  const bool is_final = (gsym == NULL
			 ? !parameters->options().shared()
			 : gsym->final_value_is_known());
  tls::Tls_optimization tlsopt = Target_aarch64<size, big_endian>::
      optimize_tls_reloc(is_final, r_type);

  Sized_relobj_file<size, big_endian>* object = relinfo->object;
  int tls_got_offset_type;
  switch (r_type)
    {
    case elfcpp::R_AARCH64_TLSGD_ADR_PAGE21:
    case elfcpp::R_AARCH64_TLSGD_ADD_LO12_NC:  // Global-dynamic
      {
	if (tlsopt == tls::TLSOPT_TO_LE)
	  {
	    if (tls_segment == NULL)
	      {
		gold_assert(parameters->errors()->error_count() > 0
			    || issue_undefined_symbol_error(gsym));
		return aarch64_reloc_funcs::STATUS_BAD_RELOC;
	      }
	    return tls_gd_to_le(relinfo, target, rela, r_type, view,
				psymval);
	  }
	else if (tlsopt == tls::TLSOPT_NONE)
	  {
	    tls_got_offset_type = GOT_TYPE_TLS_PAIR;
	    // Firstly get the address for the got entry.
	    typename elfcpp::Elf_types<size>::Elf_Addr got_entry_address;
	    if (gsym != NULL)
	      {
		gold_assert(gsym->has_got_offset(tls_got_offset_type));
		got_entry_address = target->got_->address() +
				    gsym->got_offset(tls_got_offset_type);
	      }
	    else
	      {
		unsigned int r_sym = elfcpp::elf_r_sym<size>(rela.get_r_info());
		gold_assert(
		  object->local_has_got_offset(r_sym, tls_got_offset_type));
		got_entry_address = target->got_->address() +
		  object->local_got_offset(r_sym, tls_got_offset_type);
	      }

	    // Relocate the address into adrp/ld, adrp/add pair.
	    switch (r_type)
	      {
	      case elfcpp::R_AARCH64_TLSGD_ADR_PAGE21:
		return aarch64_reloc_funcs::adrp(
		  view, got_entry_address + addend, address);

		break;

	      case elfcpp::R_AARCH64_TLSGD_ADD_LO12_NC:
		return aarch64_reloc_funcs::template rela_general<32>(
		  view, got_entry_address, addend, reloc_property);
		break;

	      default:
		gold_unreachable();
	      }
	  }
	gold_error_at_location(relinfo, relnum, rela.get_r_offset(),
			       _("unsupported gd_to_ie relaxation on %u"),
			       r_type);
      }
      break;

    case elfcpp::R_AARCH64_TLSLD_ADR_PAGE21:
    case elfcpp::R_AARCH64_TLSLD_ADD_LO12_NC:  // Local-dynamic
      {
	if (tlsopt == tls::TLSOPT_TO_LE)
	  {
	    if (tls_segment == NULL)
	      {
		gold_assert(parameters->errors()->error_count() > 0
			    || issue_undefined_symbol_error(gsym));
		return aarch64_reloc_funcs::STATUS_BAD_RELOC;
	      }
	    return this->tls_ld_to_le(relinfo, target, rela, r_type, view,
				      psymval);
	  }

	gold_assert(tlsopt == tls::TLSOPT_NONE);
	// Relocate the field with the offset of the GOT entry for
	// the module index.
	typename elfcpp::Elf_types<size>::Elf_Addr got_entry_address;
	got_entry_address = (target->got_mod_index_entry(NULL, NULL, NULL) +
			     target->got_->address());

	switch (r_type)
	  {
	  case elfcpp::R_AARCH64_TLSLD_ADR_PAGE21:
	    return aarch64_reloc_funcs::adrp(
	      view, got_entry_address + addend, address);
	    break;

	  case elfcpp::R_AARCH64_TLSLD_ADD_LO12_NC:
	    return aarch64_reloc_funcs::template rela_general<32>(
	      view, got_entry_address, addend, reloc_property);
	    break;

	  default:
	    gold_unreachable();
	  }
      }
      break;

    case elfcpp::R_AARCH64_TLSLD_MOVW_DTPREL_G1:
    case elfcpp::R_AARCH64_TLSLD_MOVW_DTPREL_G0_NC:  // Other local-dynamic
      {
	AArch64_address value = psymval->value(object, 0);
	if (tlsopt == tls::TLSOPT_TO_LE)
	  {
	    if (tls_segment == NULL)
	      {
		gold_assert(parameters->errors()->error_count() > 0
			    || issue_undefined_symbol_error(gsym));
		return aarch64_reloc_funcs::STATUS_BAD_RELOC;
	      }
	  // If building executable, _TLS_MODULE_BASE_ points to segment
	  // end. Thus we must subtract it from value.
	  value -= tls_segment->memsz();
	  }
	switch (r_type)
	  {
	  case elfcpp::R_AARCH64_TLSLD_MOVW_DTPREL_G1:
	    return aarch64_reloc_funcs::movnz(view, value + addend,
					      reloc_property);
	    break;

	  case elfcpp::R_AARCH64_TLSLD_MOVW_DTPREL_G0_NC:
	    return aarch64_reloc_funcs::template rela_general<32>(
		view, value, addend, reloc_property);
	    break;

	  default:
	    gold_unreachable();
	  }
	// We should never reach here.
      }
      break;

    case elfcpp::R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
    case elfcpp::R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:  // Initial-exec
      {
	if (tlsopt == tls::TLSOPT_TO_LE)
	  {
	    if (tls_segment == NULL)
	      {
		gold_assert(parameters->errors()->error_count() > 0
			    || issue_undefined_symbol_error(gsym));
		return aarch64_reloc_funcs::STATUS_BAD_RELOC;
	      }
	    return tls_ie_to_le(relinfo, target, rela, r_type, view,
				psymval);
	  }
	tls_got_offset_type = GOT_TYPE_TLS_OFFSET;

	// Firstly get the address for the got entry.
	typename elfcpp::Elf_types<size>::Elf_Addr got_entry_address;
	if (gsym != NULL)
	  {
	    gold_assert(gsym->has_got_offset(tls_got_offset_type));
	    got_entry_address = target->got_->address() +
				gsym->got_offset(tls_got_offset_type);
	  }
	else
	  {
	    unsigned int r_sym = elfcpp::elf_r_sym<size>(rela.get_r_info());
	    gold_assert(
		object->local_has_got_offset(r_sym, tls_got_offset_type));
	    got_entry_address = target->got_->address() +
		object->local_got_offset(r_sym, tls_got_offset_type);
	  }
	// Relocate the address into adrp/ld, adrp/add pair.
	switch (r_type)
	  {
	  case elfcpp::R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
	    return aarch64_reloc_funcs::adrp(view, got_entry_address + addend,
					     address);
	    break;
	  case elfcpp::R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
	    return aarch64_reloc_funcs::template rela_general<32>(
	      view, got_entry_address, addend, reloc_property);
	  default:
	    gold_unreachable();
	  }
      }
      // We shall never reach here.
      break;

    case elfcpp::R_AARCH64_TLSLE_ADD_TPREL_HI12:
    case elfcpp::R_AARCH64_TLSLE_ADD_TPREL_LO12:
    case elfcpp::R_AARCH64_TLSLE_ADD_TPREL_LO12_NC:
      {
	gold_assert(tls_segment != NULL);
	AArch64_address value = psymval->value(object, 0);

	if (!parameters->options().shared())
	  {
	    AArch64_address aligned_tcb_size =
		align_address(target->tcb_size(),
			      tls_segment->maximum_alignment());
	    return aarch64_reloc_funcs::template
		rela_general<32>(view,
				 value + aligned_tcb_size,
				 addend,
				 reloc_property);
	  }
	else
	  gold_error(_("%s: unsupported reloc %u "
		       "in non-static TLSLE mode."),
		     object->name().c_str(), r_type);
      }
      break;

    case elfcpp::R_AARCH64_TLSDESC_ADR_PAGE21:
    case elfcpp::R_AARCH64_TLSDESC_LD64_LO12:
    case elfcpp::R_AARCH64_TLSDESC_ADD_LO12:
    case elfcpp::R_AARCH64_TLSDESC_CALL:
      {
	if (tlsopt == tls::TLSOPT_TO_LE)
	  {
	    if (tls_segment == NULL)
	      {
		gold_assert(parameters->errors()->error_count() > 0
			    || issue_undefined_symbol_error(gsym));
		return aarch64_reloc_funcs::STATUS_BAD_RELOC;
	      }
	    return tls_desc_gd_to_le(relinfo, target, rela, r_type,
				     view, psymval);
	  }
	else
	  {
	    tls_got_offset_type = (tlsopt == tls::TLSOPT_TO_IE
				   ? GOT_TYPE_TLS_OFFSET
				   : GOT_TYPE_TLS_DESC);
	    unsigned int got_tlsdesc_offset = 0;
	    if (r_type != elfcpp::R_AARCH64_TLSDESC_CALL
		&& tlsopt == tls::TLSOPT_NONE)
	      {
		// We created GOT entries in the .got.tlsdesc portion of the
		// .got.plt section, but the offset stored in the symbol is the
		// offset within .got.tlsdesc.
		got_tlsdesc_offset = (target->got_->data_size()
				      + target->got_plt_section()->data_size());
	      }
	    typename elfcpp::Elf_types<size>::Elf_Addr got_entry_address;
	    if (gsym != NULL)
	      {
		gold_assert(gsym->has_got_offset(tls_got_offset_type));
		got_entry_address = target->got_->address()
				    + got_tlsdesc_offset
				    + gsym->got_offset(tls_got_offset_type);
	      }
	    else
	      {
		unsigned int r_sym = elfcpp::elf_r_sym<size>(rela.get_r_info());
		gold_assert(
		    object->local_has_got_offset(r_sym, tls_got_offset_type));
		got_entry_address = target->got_->address() +
		  got_tlsdesc_offset +
		  object->local_got_offset(r_sym, tls_got_offset_type);
	      }
	    if (tlsopt == tls::TLSOPT_TO_IE)
	      {
		if (tls_segment == NULL)
		  {
		    gold_assert(parameters->errors()->error_count() > 0
				|| issue_undefined_symbol_error(gsym));
		    return aarch64_reloc_funcs::STATUS_BAD_RELOC;
		  }
		return tls_desc_gd_to_ie(relinfo, target, rela, r_type,
					 view, psymval, got_entry_address,
					 address);
	      }

	    // Now do tlsdesc relocation.
	    switch (r_type)
	      {
	      case elfcpp::R_AARCH64_TLSDESC_ADR_PAGE21:
		return aarch64_reloc_funcs::adrp(view,
						 got_entry_address + addend,
						 address);
		break;
	      case elfcpp::R_AARCH64_TLSDESC_LD64_LO12:
	      case elfcpp::R_AARCH64_TLSDESC_ADD_LO12:
		return aarch64_reloc_funcs::template rela_general<32>(
		  view, got_entry_address, addend, reloc_property);
		break;
	      case elfcpp::R_AARCH64_TLSDESC_CALL:
		return aarch64_reloc_funcs::STATUS_OKAY;
		break;
	      default:
		gold_unreachable();
	      }
	  }
	}
      break;

    default:
      gold_error(_("%s: unsupported TLS reloc %u."),
		 object->name().c_str(), r_type);
    }
  return aarch64_reloc_funcs::STATUS_BAD_RELOC;
}  // End of relocate_tls.


template<int size, bool big_endian>
inline
typename AArch64_relocate_functions<size, big_endian>::Status
Target_aarch64<size, big_endian>::Relocate::tls_gd_to_le(
	     const Relocate_info<size, big_endian>* relinfo,
	     Target_aarch64<size, big_endian>* target,
	     const elfcpp::Rela<size, big_endian>& rela,
	     unsigned int r_type,
	     unsigned char* view,
	     const Symbol_value<size>* psymval)
{
  typedef AArch64_relocate_functions<size, big_endian> aarch64_reloc_funcs;
  typedef typename elfcpp::Swap<32, big_endian>::Valtype Insntype;
  typedef typename elfcpp::Elf_types<size>::Elf_Addr AArch64_address;

  Insntype* ip = reinterpret_cast<Insntype*>(view);
  Insntype insn1 = elfcpp::Swap<32, big_endian>::readval(ip);
  Insntype insn2 = elfcpp::Swap<32, big_endian>::readval(ip + 1);
  Insntype insn3 = elfcpp::Swap<32, big_endian>::readval(ip + 2);

  if (r_type == elfcpp::R_AARCH64_TLSGD_ADD_LO12_NC)
    {
      // This is the 2nd relocs, optimization should already have been
      // done.
      gold_assert((insn1 & 0xfff00000) == 0x91400000);
      return aarch64_reloc_funcs::STATUS_OKAY;
    }

  // The original sequence is -
  //   90000000        adrp    x0, 0 <main>
  //   91000000        add     x0, x0, #0x0
  //   94000000        bl      0 <__tls_get_addr>
  // optimized to sequence -
  //   d53bd040        mrs     x0, tpidr_el0
  //   91400000        add     x0, x0, #0x0, lsl #12
  //   91000000        add     x0, x0, #0x0

  // Unlike tls_ie_to_le, we change the 3 insns in one function call when we
  // encounter the first relocation "R_AARCH64_TLSGD_ADR_PAGE21". Because we
  // have to change "bl tls_get_addr", which does not have a corresponding tls
  // relocation type. So before proceeding, we need to make sure compiler
  // does not change the sequence.
  if(!(insn1 == 0x90000000      // adrp x0,0
       && insn2 == 0x91000000   // add x0, x0, #0x0
       && insn3 == 0x94000000)) // bl 0
    {
      // Ideally we should give up gd_to_le relaxation and do gd access.
      // However the gd_to_le relaxation decision has been made early
      // in the scan stage, where we did not allocate any GOT entry for
      // this symbol. Therefore we have to exit and report error now.
      gold_error(_("unexpected reloc insn sequence while relaxing "
		   "tls gd to le for reloc %u."), r_type);
      return aarch64_reloc_funcs::STATUS_BAD_RELOC;
    }

  // Write new insns.
  insn1 = 0xd53bd040;  // mrs x0, tpidr_el0
  insn2 = 0x91400000;  // add x0, x0, #0x0, lsl #12
  insn3 = 0x91000000;  // add x0, x0, #0x0
  elfcpp::Swap<32, big_endian>::writeval(ip, insn1);
  elfcpp::Swap<32, big_endian>::writeval(ip + 1, insn2);
  elfcpp::Swap<32, big_endian>::writeval(ip + 2, insn3);

  // Calculate tprel value.
  Output_segment* tls_segment = relinfo->layout->tls_segment();
  gold_assert(tls_segment != NULL);
  AArch64_address value = psymval->value(relinfo->object, 0);
  const elfcpp::Elf_Xword addend = rela.get_r_addend();
  AArch64_address aligned_tcb_size =
      align_address(target->tcb_size(), tls_segment->maximum_alignment());
  AArch64_address x = value + aligned_tcb_size;

  // After new insns are written, apply TLSLE relocs.
  const AArch64_reloc_property* rp1 =
      aarch64_reloc_property_table->get_reloc_property(
	  elfcpp::R_AARCH64_TLSLE_ADD_TPREL_HI12);
  const AArch64_reloc_property* rp2 =
      aarch64_reloc_property_table->get_reloc_property(
	  elfcpp::R_AARCH64_TLSLE_ADD_TPREL_LO12);
  gold_assert(rp1 != NULL && rp2 != NULL);

  typename aarch64_reloc_funcs::Status s1 =
      aarch64_reloc_funcs::template rela_general<32>(view + 4,
						     x,
						     addend,
						     rp1);
  if (s1 != aarch64_reloc_funcs::STATUS_OKAY)
    return s1;

  typename aarch64_reloc_funcs::Status s2 =
      aarch64_reloc_funcs::template rela_general<32>(view + 8,
						     x,
						     addend,
						     rp2);

  this->skip_call_tls_get_addr_ = true;
  return s2;
}  // End of tls_gd_to_le


template<int size, bool big_endian>
inline
typename AArch64_relocate_functions<size, big_endian>::Status
Target_aarch64<size, big_endian>::Relocate::tls_ld_to_le(
	     const Relocate_info<size, big_endian>* relinfo,
	     Target_aarch64<size, big_endian>* target,
	     const elfcpp::Rela<size, big_endian>& rela,
	     unsigned int r_type,
	     unsigned char* view,
	     const Symbol_value<size>* psymval)
{
  typedef AArch64_relocate_functions<size, big_endian> aarch64_reloc_funcs;
  typedef typename elfcpp::Swap<32, big_endian>::Valtype Insntype;
  typedef typename elfcpp::Elf_types<size>::Elf_Addr AArch64_address;

  Insntype* ip = reinterpret_cast<Insntype*>(view);
  Insntype insn1 = elfcpp::Swap<32, big_endian>::readval(ip);
  Insntype insn2 = elfcpp::Swap<32, big_endian>::readval(ip + 1);
  Insntype insn3 = elfcpp::Swap<32, big_endian>::readval(ip + 2);

  if (r_type == elfcpp::R_AARCH64_TLSLD_ADD_LO12_NC)
    {
      // This is the 2nd relocs, optimization should already have been
      // done.
      gold_assert((insn1 & 0xfff00000) == 0x91400000);
      return aarch64_reloc_funcs::STATUS_OKAY;
    }

  // The original sequence is -
  //   90000000        adrp    x0, 0 <main>
  //   91000000        add     x0, x0, #0x0
  //   94000000        bl      0 <__tls_get_addr>
  // optimized to sequence -
  //   d53bd040        mrs     x0, tpidr_el0
  //   91400000        add     x0, x0, #0x0, lsl #12
  //   91000000        add     x0, x0, #0x0

  // Unlike tls_ie_to_le, we change the 3 insns in one function call when we
  // encounter the first relocation "R_AARCH64_TLSLD_ADR_PAGE21". Because we
  // have to change "bl tls_get_addr", which does not have a corresponding tls
  // relocation type. So before proceeding, we need to make sure compiler
  // does not change the sequence.
  if(!(insn1 == 0x90000000      // adrp x0,0
       && insn2 == 0x91000000   // add x0, x0, #0x0
       && insn3 == 0x94000000)) // bl 0
    {
      // Ideally we should give up gd_to_le relaxation and do gd access.
      // However the gd_to_le relaxation decision has been made early
      // in the scan stage, where we did not allocate any GOT entry for
      // this symbol. Therefore we have to exit and report error now.
      gold_error(_("unexpected reloc insn sequence while relaxing "
		   "tls gd to le for reloc %u."), r_type);
      return aarch64_reloc_funcs::STATUS_BAD_RELOC;
    }

  // Write new insns.
  insn1 = 0xd53bd040;  // mrs x0, tpidr_el0
  insn2 = 0x91400000;  // add x0, x0, #0x0, lsl #12
  insn3 = 0x91000000;  // add x0, x0, #0x0
  elfcpp::Swap<32, big_endian>::writeval(ip, insn1);
  elfcpp::Swap<32, big_endian>::writeval(ip + 1, insn2);
  elfcpp::Swap<32, big_endian>::writeval(ip + 2, insn3);

  // Calculate tprel value.
  Output_segment* tls_segment = relinfo->layout->tls_segment();
  gold_assert(tls_segment != NULL);
  AArch64_address value = psymval->value(relinfo->object, 0);
  const elfcpp::Elf_Xword addend = rela.get_r_addend();
  AArch64_address aligned_tcb_size =
      align_address(target->tcb_size(), tls_segment->maximum_alignment());
  AArch64_address x = value + aligned_tcb_size;

  // After new insns are written, apply TLSLE relocs.
  const AArch64_reloc_property* rp1 =
      aarch64_reloc_property_table->get_reloc_property(
	  elfcpp::R_AARCH64_TLSLE_ADD_TPREL_HI12);
  const AArch64_reloc_property* rp2 =
      aarch64_reloc_property_table->get_reloc_property(
	  elfcpp::R_AARCH64_TLSLE_ADD_TPREL_LO12);
  gold_assert(rp1 != NULL && rp2 != NULL);

  typename aarch64_reloc_funcs::Status s1 =
      aarch64_reloc_funcs::template rela_general<32>(view + 4,
						     x,
						     addend,
						     rp1);
  if (s1 != aarch64_reloc_funcs::STATUS_OKAY)
    return s1;

  typename aarch64_reloc_funcs::Status s2 =
      aarch64_reloc_funcs::template rela_general<32>(view + 8,
						     x,
						     addend,
						     rp2);

  this->skip_call_tls_get_addr_ = true;
  return s2;

}  // End of tls_ld_to_le

template<int size, bool big_endian>
inline
typename AArch64_relocate_functions<size, big_endian>::Status
Target_aarch64<size, big_endian>::Relocate::tls_ie_to_le(
	     const Relocate_info<size, big_endian>* relinfo,
	     Target_aarch64<size, big_endian>* target,
	     const elfcpp::Rela<size, big_endian>& rela,
	     unsigned int r_type,
	     unsigned char* view,
	     const Symbol_value<size>* psymval)
{
  typedef typename elfcpp::Elf_types<size>::Elf_Addr AArch64_address;
  typedef typename elfcpp::Swap<32, big_endian>::Valtype Insntype;
  typedef AArch64_relocate_functions<size, big_endian> aarch64_reloc_funcs;

  AArch64_address value = psymval->value(relinfo->object, 0);
  Output_segment* tls_segment = relinfo->layout->tls_segment();
  AArch64_address aligned_tcb_address =
      align_address(target->tcb_size(), tls_segment->maximum_alignment());
  const elfcpp::Elf_Xword addend = rela.get_r_addend();
  AArch64_address x = value + addend + aligned_tcb_address;
  // "x" is the offset to tp, we can only do this if x is within
  // range [0, 2^32-1]
  if (!(size == 32 || (size == 64 && (static_cast<uint64_t>(x) >> 32) == 0)))
    {
      gold_error(_("TLS variable referred by reloc %u is too far from TP."),
		 r_type);
      return aarch64_reloc_funcs::STATUS_BAD_RELOC;
    }

  Insntype* ip = reinterpret_cast<Insntype*>(view);
  Insntype insn = elfcpp::Swap<32, big_endian>::readval(ip);
  unsigned int regno;
  Insntype newinsn;
  if (r_type == elfcpp::R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21)
    {
      // Generate movz.
      regno = (insn & 0x1f);
      newinsn = (0xd2a00000 | regno) | (((x >> 16) & 0xffff) << 5);
    }
  else if (r_type == elfcpp::R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC)
    {
      // Generate movk.
      regno = (insn & 0x1f);
      gold_assert(regno == ((insn >> 5) & 0x1f));
      newinsn = (0xf2800000 | regno) | ((x & 0xffff) << 5);
    }
  else
    gold_unreachable();

  elfcpp::Swap<32, big_endian>::writeval(ip, newinsn);
  return aarch64_reloc_funcs::STATUS_OKAY;
}  // End of tls_ie_to_le


template<int size, bool big_endian>
inline
typename AArch64_relocate_functions<size, big_endian>::Status
Target_aarch64<size, big_endian>::Relocate::tls_desc_gd_to_le(
	     const Relocate_info<size, big_endian>* relinfo,
	     Target_aarch64<size, big_endian>* target,
	     const elfcpp::Rela<size, big_endian>& rela,
	     unsigned int r_type,
	     unsigned char* view,
	     const Symbol_value<size>* psymval)
{
  typedef typename elfcpp::Elf_types<size>::Elf_Addr AArch64_address;
  typedef typename elfcpp::Swap<32, big_endian>::Valtype Insntype;
  typedef AArch64_relocate_functions<size, big_endian> aarch64_reloc_funcs;

  // TLSDESC-GD sequence is like:
  //   adrp  x0, :tlsdesc:v1
  //   ldr   x1, [x0, #:tlsdesc_lo12:v1]
  //   add   x0, x0, :tlsdesc_lo12:v1
  //   .tlsdesccall    v1
  //   blr   x1
  // After desc_gd_to_le optimization, the sequence will be like:
  //   movz  x0, #0x0, lsl #16
  //   movk  x0, #0x10
  //   nop
  //   nop

  // Calculate tprel value.
  Output_segment* tls_segment = relinfo->layout->tls_segment();
  gold_assert(tls_segment != NULL);
  Insntype* ip = reinterpret_cast<Insntype*>(view);
  const elfcpp::Elf_Xword addend = rela.get_r_addend();
  AArch64_address value = psymval->value(relinfo->object, addend);
  AArch64_address aligned_tcb_size =
      align_address(target->tcb_size(), tls_segment->maximum_alignment());
  AArch64_address x = value + aligned_tcb_size;
  // x is the offset to tp, we can only do this if x is within range
  // [0, 2^32-1]. If x is out of range, fail and exit.
  if (size == 64 && (static_cast<uint64_t>(x) >> 32) != 0)
    {
      gold_error(_("TLS variable referred by reloc %u is too far from TP. "
		   "We Can't do gd_to_le relaxation.\n"), r_type);
      return aarch64_reloc_funcs::STATUS_BAD_RELOC;
    }
  Insntype newinsn;
  switch (r_type)
    {
    case elfcpp::R_AARCH64_TLSDESC_ADD_LO12:
    case elfcpp::R_AARCH64_TLSDESC_CALL:
      // Change to nop
      newinsn = 0xd503201f;
      break;

    case elfcpp::R_AARCH64_TLSDESC_ADR_PAGE21:
      // Change to movz.
      newinsn = 0xd2a00000 | (((x >> 16) & 0xffff) << 5);
      break;

    case elfcpp::R_AARCH64_TLSDESC_LD64_LO12:
      // Change to movk.
      newinsn = 0xf2800000 | ((x & 0xffff) << 5);
      break;

    default:
      gold_error(_("unsupported tlsdesc gd_to_le optimization on reloc %u"),
		 r_type);
      gold_unreachable();
    }
  elfcpp::Swap<32, big_endian>::writeval(ip, newinsn);
  return aarch64_reloc_funcs::STATUS_OKAY;
}  // End of tls_desc_gd_to_le


template<int size, bool big_endian>
inline
typename AArch64_relocate_functions<size, big_endian>::Status
Target_aarch64<size, big_endian>::Relocate::tls_desc_gd_to_ie(
	     const Relocate_info<size, big_endian>* /* relinfo */,
	     Target_aarch64<size, big_endian>* /* target */,
	     const elfcpp::Rela<size, big_endian>& rela,
	     unsigned int r_type,
	     unsigned char* view,
	     const Symbol_value<size>* /* psymval */,
	     typename elfcpp::Elf_types<size>::Elf_Addr got_entry_address,
	     typename elfcpp::Elf_types<size>::Elf_Addr address)
{
  typedef typename elfcpp::Swap<32, big_endian>::Valtype Insntype;
  typedef AArch64_relocate_functions<size, big_endian> aarch64_reloc_funcs;

  // TLSDESC-GD sequence is like:
  //   adrp  x0, :tlsdesc:v1
  //   ldr   x1, [x0, #:tlsdesc_lo12:v1]
  //   add   x0, x0, :tlsdesc_lo12:v1
  //   .tlsdesccall    v1
  //   blr   x1
  // After desc_gd_to_ie optimization, the sequence will be like:
  //   adrp  x0, :tlsie:v1
  //   ldr   x0, [x0, :tlsie_lo12:v1]
  //   nop
  //   nop

  Insntype* ip = reinterpret_cast<Insntype*>(view);
  const elfcpp::Elf_Xword addend = rela.get_r_addend();
  Insntype newinsn;
  switch (r_type)
    {
    case elfcpp::R_AARCH64_TLSDESC_ADD_LO12:
    case elfcpp::R_AARCH64_TLSDESC_CALL:
      // Change to nop
      newinsn = 0xd503201f;
      elfcpp::Swap<32, big_endian>::writeval(ip, newinsn);
      break;

    case elfcpp::R_AARCH64_TLSDESC_ADR_PAGE21:
      {
	return aarch64_reloc_funcs::adrp(view, got_entry_address + addend,
					 address);
      }
      break;

    case elfcpp::R_AARCH64_TLSDESC_LD64_LO12:
      {
	const AArch64_reloc_property* reloc_property =
	    aarch64_reloc_property_table->get_reloc_property(
	      elfcpp::R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC);
	return aarch64_reloc_funcs::template rela_general<32>(
		 view, got_entry_address, addend, reloc_property);
      }
      break;

    default:
      gold_error(_("Don't support tlsdesc gd_to_ie optimization on reloc %u"),
		 r_type);
      gold_unreachable();
    }
  return aarch64_reloc_funcs::STATUS_OKAY;
}  // End of tls_desc_gd_to_ie

// Relocate section data.

template<int size, bool big_endian>
void
Target_aarch64<size, big_endian>::relocate_section(
    const Relocate_info<size, big_endian>* relinfo,
    unsigned int sh_type,
    const unsigned char* prelocs,
    size_t reloc_count,
    Output_section* output_section,
    bool needs_special_offset_handling,
    unsigned char* view,
    typename elfcpp::Elf_types<size>::Elf_Addr address,
    section_size_type view_size,
    const Reloc_symbol_changes* reloc_symbol_changes)
{
  gold_assert(sh_type == elfcpp::SHT_RELA);
  typedef typename Target_aarch64<size, big_endian>::Relocate AArch64_relocate;
  gold::relocate_section<size, big_endian, Target_aarch64, elfcpp::SHT_RELA,
			 AArch64_relocate, gold::Default_comdat_behavior>(
    relinfo,
    this,
    prelocs,
    reloc_count,
    output_section,
    needs_special_offset_handling,
    view,
    address,
    view_size,
    reloc_symbol_changes);
}

// Return the size of a relocation while scanning during a relocatable
// link.

template<int size, bool big_endian>
unsigned int
Target_aarch64<size, big_endian>::Relocatable_size_for_reloc::
get_size_for_reloc(
    unsigned int ,
    Relobj* )
{
  // We will never support SHT_REL relocations.
  gold_unreachable();
  return 0;
}

// Scan the relocs during a relocatable link.

template<int size, bool big_endian>
void
Target_aarch64<size, big_endian>::scan_relocatable_relocs(
    Symbol_table* symtab,
    Layout* layout,
    Sized_relobj_file<size, big_endian>* object,
    unsigned int data_shndx,
    unsigned int sh_type,
    const unsigned char* prelocs,
    size_t reloc_count,
    Output_section* output_section,
    bool needs_special_offset_handling,
    size_t local_symbol_count,
    const unsigned char* plocal_symbols,
    Relocatable_relocs* rr)
{
  gold_assert(sh_type == elfcpp::SHT_RELA);

  typedef gold::Default_scan_relocatable_relocs<elfcpp::SHT_RELA,
    Relocatable_size_for_reloc> Scan_relocatable_relocs;

  gold::scan_relocatable_relocs<size, big_endian, elfcpp::SHT_RELA,
      Scan_relocatable_relocs>(
    symtab,
    layout,
    object,
    data_shndx,
    prelocs,
    reloc_count,
    output_section,
    needs_special_offset_handling,
    local_symbol_count,
    plocal_symbols,
    rr);
}

// Relocate a section during a relocatable link.

template<int size, bool big_endian>
void
Target_aarch64<size, big_endian>::relocate_relocs(
    const Relocate_info<size, big_endian>* relinfo,
    unsigned int sh_type,
    const unsigned char* prelocs,
    size_t reloc_count,
    Output_section* output_section,
    typename elfcpp::Elf_types<size>::Elf_Off offset_in_output_section,
    const Relocatable_relocs* rr,
    unsigned char* view,
    typename elfcpp::Elf_types<size>::Elf_Addr view_address,
    section_size_type view_size,
    unsigned char* reloc_view,
    section_size_type reloc_view_size)
{
  gold_assert(sh_type == elfcpp::SHT_RELA);

  gold::relocate_relocs<size, big_endian, elfcpp::SHT_RELA>(
    relinfo,
    prelocs,
    reloc_count,
    output_section,
    offset_in_output_section,
    rr,
    view,
    view_address,
    view_size,
    reloc_view,
    reloc_view_size);
}


// The selector for aarch64 object files.

template<int size, bool big_endian>
class Target_selector_aarch64 : public Target_selector
{
 public:
  Target_selector_aarch64();

  virtual Target*
  do_instantiate_target()
  { return new Target_aarch64<size, big_endian>(); }
};

template<>
Target_selector_aarch64<32, true>::Target_selector_aarch64()
  : Target_selector(elfcpp::EM_AARCH64, 32, true,
		    "elf32-bigaarch64", "aarch64_elf32_be_vec")
{ }

template<>
Target_selector_aarch64<32, false>::Target_selector_aarch64()
  : Target_selector(elfcpp::EM_AARCH64, 32, false,
		    "elf32-littleaarch64", "aarch64_elf32_le_vec")
{ }

template<>
Target_selector_aarch64<64, true>::Target_selector_aarch64()
  : Target_selector(elfcpp::EM_AARCH64, 64, true,
		    "elf64-bigaarch64", "aarch64_elf64_be_vec")
{ }

template<>
Target_selector_aarch64<64, false>::Target_selector_aarch64()
  : Target_selector(elfcpp::EM_AARCH64, 64, false,
		    "elf64-littleaarch64", "aarch64_elf64_le_vec")
{ }

Target_selector_aarch64<32, true> target_selector_aarch64elf32b;
Target_selector_aarch64<32, false> target_selector_aarch64elf32;
Target_selector_aarch64<64, true> target_selector_aarch64elfb;
Target_selector_aarch64<64, false> target_selector_aarch64elf;

} // End anonymous namespace.
