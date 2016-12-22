// Copyright 2016 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cpu_instructions/x86/cleanup_instruction_set_fix_operands.h"

#include <algorithm>
#include <iterator>
#include "strings/string.h"
#include <unordered_set>
#include <vector>

#include "glog/logging.h"
#include "src/google/protobuf/repeated_field.h"
#include "strings/str_cat.h"
#include "util/gtl/map_util.h"
#include "cpu_instructions/base/cleanup_instruction_set.h"
#include "cpu_instructions/proto/instructions.pb.h"
#include "cpu_instructions/x86/cleanup_instruction_set_utils.h"
#include "util/task/canonical_errors.h"
#include "util/task/status.h"

namespace cpu_instructions {
namespace x86 {
namespace {

using ::google::protobuf::RepeatedPtrField;
using ::cpu_instructions::util::InvalidArgumentError;
using ::cpu_instructions::util::Status;

// Mapping from memory operands to their sizes as used in the Intel assembly
// syntax.
const std::pair<const char*, const char*> kOperandToPointerSize[] = {
    {"m8", "BYTE"}, {"m16", "WORD"}, {"m32", "DWORD"}, {"m64", "QWORD"}};

// List of RSI-indexed source arrays.
const char* kRSIIndexes[] = {"BYTE PTR [RSI]", "WORD PTR [RSI]",
                             "DWORD PTR [RSI]", "QWORD PTR [RSI]"};

// List of RDI-indexed destination arrays.
const char* kRDIIndexes[] = {"BYTE PTR [RDI]", "WORD PTR [RDI]",
                             "DWORD PTR [RDI]", "QWORD PTR [RDI]"};

}  // namespace

Status FixOperandsOfCmpsAndMovs(InstructionSetProto* instruction_set) {
  CHECK(instruction_set != nullptr);
  const std::unordered_set<string> kMnemonics = {"CMPS", "MOVS"};
  const std::unordered_set<string> kSourceOperands(std::begin(kRSIIndexes),
                                                   std::begin(kRSIIndexes));
  const std::unordered_set<string> kDestinationOperands(
      std::begin(kRDIIndexes), std::begin(kRDIIndexes));
  const std::unordered_map<string, string> operand_to_pointer_size(
      std::begin(kOperandToPointerSize), std::end(kOperandToPointerSize));
  Status status = Status::OK;
  for (InstructionProto& instruction :
       *instruction_set->mutable_instructions()) {
    InstructionFormat* const vendor_syntax =
        instruction.mutable_vendor_syntax();
    if (!ContainsKey(kMnemonics, vendor_syntax->mnemonic())) {
      continue;
    }

    if (vendor_syntax->operands_size() != 2) {
      status = InvalidArgumentError(
          "Unexpected number of operands of a CMPS/MOVS instruction.");
      LOG(ERROR) << status;
      continue;
    }
    string pointer_size;
    if (!FindCopy(operand_to_pointer_size, vendor_syntax->operands(0).name(),
                  &pointer_size) &&
        !ContainsKey(kSourceOperands, vendor_syntax->operands(0).name()) &&
        !ContainsKey(kDestinationOperands, vendor_syntax->operands(0).name())) {
      status = InvalidArgumentError(
          StrCat("Unexpected operand of a CMPS/MOVS instruction: ",
                 vendor_syntax->operands(0).name()));
      LOG(ERROR) << status;
      continue;
    }
    CHECK_EQ(vendor_syntax->operands_size(), 2);
    // The correct syntax for MOVS is MOVSB BYTE PTR [RDI],BYTE PTR [RSI]
    // (destination is the right operand, as expected in the Intel syntax),
    // while for CMPS LLVM only supports CMPSB BYTE PTR [RSI],BYTE PTR [RDI].
    // The following handles this.
    static const char* const kIndexings[] = {"[RDI]", "[RSI]"};
    const int dest = vendor_syntax->mnemonic() == "MOVS" ? 0 : 1;
    const int src = 1 - dest;
    vendor_syntax->mutable_operands(0)->set_name(
        StrCat(pointer_size, " PTR ", kIndexings[dest]));
    vendor_syntax->mutable_operands(0)->set_usage(
        dest == 0 ? InstructionOperand::USAGE_WRITE
                  : InstructionOperand::USAGE_READ);
    vendor_syntax->mutable_operands(1)->set_name(
        StrCat(pointer_size, " PTR ", kIndexings[src]));
    vendor_syntax->mutable_operands(1)->set_usage(
        InstructionOperand::USAGE_READ);
  }
  return status;
}
REGISTER_INSTRUCTION_SET_TRANSFORM(FixOperandsOfCmpsAndMovs, 2000);

Status FixOperandsOfInsAndOuts(InstructionSetProto* instruction_set) {
  static const char kIns[] = "INS";
  static const char kOuts[] = "OUTS";
  const std::unordered_map<string, string> operand_to_pointer_size(
      std::begin(kOperandToPointerSize), std::end(kOperandToPointerSize));
  Status status = Status::OK;
  for (InstructionProto& instruction :
       *instruction_set->mutable_instructions()) {
    InstructionFormat* const vendor_syntax =
        instruction.mutable_vendor_syntax();
    const bool is_ins = vendor_syntax->mnemonic() == kIns;
    const bool is_outs = vendor_syntax->mnemonic() == kOuts;
    if (!is_ins && !is_outs) {
      continue;
    }

    if (vendor_syntax->operands_size() != 2) {
      status = InvalidArgumentError(
          "Unexpected number of operands of an INS/OUTS instruction.");
      LOG(ERROR) << status;
      continue;
    }
    string pointer_size;
    if (!FindCopy(operand_to_pointer_size, vendor_syntax->operands(0).name(),
                  &pointer_size) &&
        !FindCopy(operand_to_pointer_size, vendor_syntax->operands(1).name(),
                  &pointer_size)) {
      status = InvalidArgumentError(
          StrCat("Unexpected operands of an INS/OUTS instruction: ",
                 vendor_syntax->operands(0).name(), ", ",
                 vendor_syntax->operands(1).name()));
      LOG(ERROR) << status;
      continue;
    }
    CHECK_EQ(vendor_syntax->operands_size(), 2);
    if (is_ins) {
      vendor_syntax->mutable_operands(0)->set_name(
          StrCat(pointer_size, " PTR [RDI]"));
      vendor_syntax->mutable_operands(0)->set_usage(
          InstructionOperand::USAGE_WRITE);
      vendor_syntax->mutable_operands(1)->set_name("DX");
      vendor_syntax->mutable_operands(1)->set_usage(
          InstructionOperand::USAGE_READ);
    } else {
      CHECK(is_outs);
      vendor_syntax->mutable_operands(0)->set_name("DX");
      vendor_syntax->mutable_operands(0)->set_usage(
          InstructionOperand::USAGE_READ);
      vendor_syntax->mutable_operands(1)->set_name(
          StrCat(pointer_size, " PTR [RSI]"));
      vendor_syntax->mutable_operands(1)->set_usage(
          InstructionOperand::USAGE_READ);
    }
  }
  return status;
}
REGISTER_INSTRUCTION_SET_TRANSFORM(FixOperandsOfInsAndOuts, 2000);

Status FixOperandsOfLodsScasAndStos(InstructionSetProto* instruction_set) {
  // Note that we're matching only the versions with operands. These versions
  // use the mnemonics without the size suffix. By matching exactly these names,
  // we can easily avoid the operand-less versions.
  static const char kLods[] = "LODS";
  static const char kScas[] = "SCAS";
  static const char kStos[] = "STOS";
  const std::unordered_map<string, string> operand_to_pointer_size(
      std::begin(kOperandToPointerSize), std::end(kOperandToPointerSize));
  const std::unordered_map<string, string> kOperandToRegister = {
      {"m8", "AL"}, {"m16", "AX"}, {"m32", "EAX"}, {"m64", "RAX"}};
  Status status = Status::OK;
  for (InstructionProto& instruction :
       *instruction_set->mutable_instructions()) {
    InstructionFormat* const vendor_syntax =
        instruction.mutable_vendor_syntax();
    const bool is_lods = vendor_syntax->mnemonic() == kLods;
    const bool is_stos = vendor_syntax->mnemonic() == kStos;
    const bool is_scas = vendor_syntax->mnemonic() == kScas;
    if (!is_lods && !is_stos && !is_scas) {
      continue;
    }

    if (vendor_syntax->operands_size() != 1) {
      status = InvalidArgumentError(
          "Unexpected number of operands of a LODS/STOS instruction.");
      LOG(ERROR) << status;
      continue;
    }
    string register_operand;
    string pointer_size;
    if (!FindCopy(kOperandToRegister, vendor_syntax->operands(0).name(),
                  &register_operand) ||
        !FindCopy(operand_to_pointer_size, vendor_syntax->operands(0).name(),
                  &pointer_size)) {
      status = InvalidArgumentError(
          StrCat("Unexpected operand of a LODS/STOS instruction: ",
                 vendor_syntax->operands(0).name()));
      LOG(ERROR) << status;
      continue;
    }
    vendor_syntax->clear_operands();
    if (is_stos) {
      auto* const operand = vendor_syntax->add_operands();
      operand->set_name(StrCat(pointer_size, " PTR [RDI]"));
      operand->set_encoding(InstructionOperand::IMPLICIT_ENCODING);
      operand->set_usage(InstructionOperand::USAGE_READ);
    }
    auto* const operand = vendor_syntax->add_operands();
    operand->set_encoding(InstructionOperand::IMPLICIT_ENCODING);
    operand->set_name(register_operand);
    operand->set_usage(InstructionOperand::USAGE_READ);
    if (is_lods) {
      auto* const operand = vendor_syntax->add_operands();
      operand->set_encoding(InstructionOperand::IMPLICIT_ENCODING);
      operand->set_name(StrCat(pointer_size, " PTR [RSI]"));
      operand->set_usage(InstructionOperand::USAGE_READ);
    }
    if (is_scas) {
      auto* const operand = vendor_syntax->add_operands();
      operand->set_encoding(InstructionOperand::IMPLICIT_ENCODING);
      operand->set_name(StrCat(pointer_size, " PTR [RDI]"));
      operand->set_usage(InstructionOperand::USAGE_READ);
    }
  }
  return status;
}
REGISTER_INSTRUCTION_SET_TRANSFORM(FixOperandsOfLodsScasAndStos, 2000);

Status FixOperandsOfVMovq(InstructionSetProto* instruction_set) {
  CHECK(instruction_set != nullptr);
  static const char kVMovQEncoding[] = "VEX.128.F3.0F.WIG 7E /r";
  static const char kRegisterOrMemoryOperand[] = "xmm2/m64";
  ::google::protobuf::RepeatedPtrField<InstructionProto>* const instructions =
      instruction_set->mutable_instructions();
  for (InstructionProto& instruction : *instructions) {
    if (instruction.binary_encoding() != kVMovQEncoding) continue;

    InstructionFormat* const vendor_syntax =
        instruction.mutable_vendor_syntax();
    if (vendor_syntax->operands_size() != 2) {
      return InvalidArgumentError(
          StrCat("Unexpected number of operands of a VMOVQ instruction: ",
                 instruction.DebugString()));
    }
    vendor_syntax->mutable_operands(1)->set_name(kRegisterOrMemoryOperand);
  }
  return Status::OK;
}
REGISTER_INSTRUCTION_SET_TRANSFORM(FixOperandsOfVMovq, 2000);

Status FixRegOperands(InstructionSetProto* instruction_set) {
  CHECK(instruction_set != nullptr);
  static const char kR8Operand[] = "r8";
  static const char kR16Operand[] = "r16";
  static const char kR32Operand[] = "r32";
  static const char kR64Operand[] = "r64";
  static const char kRegOperand[] = "reg";
  // The mnemonics for which we add new entries.
  const std::unordered_set<string> kExpandToAllSizes = {"LAR"};
  // The mnemonics for which we just replace reg with r8/r16/r32.
  const std::unordered_set<string> kRenameToReg8 = {"VPBROADCASTB"};
  const std::unordered_set<string> kRenameToReg16 = {"VPBROADCASTW"};
  const std::unordered_set<string> kRenameToReg32 = {
      "EXTRACTPS", "MOVMSKPD",  "MOVMSKPS", "PEXTRB",  "PEXTRW",   "PMOVMSKB",
      "VMOVMSKPD", "VMOVMSKPS", "VPEXTRB",  "VPEXTRW", "VPMOVMSKB"};
  // We can't safely add new entries to 'instructions' while we iterate over it.
  // Instead, we collect the instructions in a separate vector and add it to the
  // proto at the end.
  std::vector<InstructionProto> new_instruction_protos;
  ::google::protobuf::RepeatedPtrField<InstructionProto>* const instructions =
      instruction_set->mutable_instructions();
  Status status = Status::OK;
  for (InstructionProto& instruction : *instructions) {
    InstructionFormat* const vendor_syntax =
        instruction.mutable_vendor_syntax();
    const string& mnemonic = vendor_syntax->mnemonic();
    for (auto& operand : *vendor_syntax->mutable_operands()) {
      if (operand.name() == kRegOperand) {
        if (ContainsKey(kExpandToAllSizes, mnemonic)) {
          // This is a bit hacky. To avoid complicated matching of registers, we
          // just override the existing entry in the instruction set proto, add
          // the modified proto to new_instruction_protos except for the last
          // modification which we keep in the instruction set proto.
          //
          // This is safe as long as there is only one reg operand per entry
          // (which is true in the current version of the data).
          operand.set_name(kR32Operand);
          new_instruction_protos.push_back(instruction);
          operand.set_name(kR64Operand);
          instruction.set_binary_encoding("REX.W + " +
                                          instruction.binary_encoding());
        } else if (ContainsKey(kRenameToReg8, mnemonic)) {
          operand.set_name(kR8Operand);
        } else if (ContainsKey(kRenameToReg16, mnemonic)) {
          operand.set_name(kR16Operand);
        } else if (ContainsKey(kRenameToReg32, mnemonic)) {
          operand.set_name(kR32Operand);
        } else {
          status = InvalidArgumentError(
              StrCat("Unexpected instruction mnemonic: ", mnemonic));
          LOG(ERROR) << status;
          continue;
        }
      }
    }
  }
  std::copy(new_instruction_protos.begin(), new_instruction_protos.end(),
            ::google::protobuf::RepeatedPtrFieldBackInserter(instructions));

  return status;
}
REGISTER_INSTRUCTION_SET_TRANSFORM(FixRegOperands, 2000);

Status RenameOperands(InstructionSetProto* instruction_set) {
  CHECK(instruction_set != nullptr);
  const std::unordered_map<string, string> kOperandRenaming = {
      // Synonyms (different names used for the same type in different parts of
      // the manual).
      {"m80dec", "m80bcd"},
      {"r8/m8", "r/m8"},
      {"r16/m16", "r/m16"},
      {"r32/m32", "r/m32"},
      {"r64/m64", "r/m64"},
      {"ST", "ST(0)"},
      // Variants that depend on the mode of the CPU. The 32- and 64-bit modes
      // always use the larger of the two values.
      {"m14/28byte", "m28byte"},
      {"m94/108byte", "m108byte"}};
  for (InstructionProto& instruction :
       *instruction_set->mutable_instructions()) {
    InstructionFormat* const vendor_syntax =
        instruction.mutable_vendor_syntax();
    for (auto& operand : *vendor_syntax->mutable_operands()) {
      const string* renaming = FindOrNull(kOperandRenaming, operand.name());
      if (renaming != nullptr) {
        operand.set_name(*renaming);
      }
    }
  }
  return Status::OK;
}
REGISTER_INSTRUCTION_SET_TRANSFORM(RenameOperands, 2000);

Status RemoveImplicitST0Operand(InstructionSetProto* instruction_set) {
  CHECK(instruction_set != nullptr);
  static const char kImplicitST0Operand[] = "ST(0)";
  const std::unordered_set<string> kUpdatedInstructionEncodings = {
      "D8 C0+i", "D8 C8+i", "D8 E0+i", "D8 E8+i", "D8 F0+i", "D8 F8+i",
      "DB E8+i", "DB F0+i", "DE C0+i", "DE C8+i", "DE E0+i", "DE E8+i",
      "DE F0+i", "DE F8+i", "DF E8+i", "DF F0+i",
  };
  for (InstructionProto& instruction :
       *instruction_set->mutable_instructions()) {
    if (!ContainsKey(kUpdatedInstructionEncodings,
                     instruction.binary_encoding())) {
      continue;
    }
    RepeatedPtrField<InstructionOperand>* const operands =
        instruction.mutable_vendor_syntax()->mutable_operands();
    operands->erase(std::remove_if(operands->begin(), operands->end(),
                                   [](const InstructionOperand& operand) {
                                     return operand.name() ==
                                            kImplicitST0Operand;
                                   }),
                    operands->end());
  }
  return Status::OK;
}
REGISTER_INSTRUCTION_SET_TRANSFORM(RemoveImplicitST0Operand, 2000);

Status RemoveImplicitXmm0Operand(InstructionSetProto* instruction_set) {
  CHECK(instruction_set != nullptr);
  static const char kImplicitXmm0Operand[] = "<XMM0>";
  for (InstructionProto& instruction :
       *instruction_set->mutable_instructions()) {
    RepeatedPtrField<InstructionOperand>* const operands =
        instruction.mutable_vendor_syntax()->mutable_operands();
    operands->erase(std::remove_if(operands->begin(), operands->end(),
                                   [](const InstructionOperand& operand) {
                                     return operand.name() ==
                                            kImplicitXmm0Operand;
                                   }),
                    operands->end());
  }
  return Status::OK;
}
REGISTER_INSTRUCTION_SET_TRANSFORM(RemoveImplicitXmm0Operand, 2000);

}  // namespace x86
}  // namespace cpu_instructions