(* libguestfs
 * Copyright (C) 2016 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *)

(* Please read generator/README first. *)

open Std_utils
open Utils
open Pr
open Docstrings

(* This code is currently only used when starting the appliance
 * on aarch64 (and probably only on RHEL).  (See lib/appliance-uefi.c)
 * It is vestigial and we should think about deleting it.
 *)

(* Order is significant *within architectures only*. *)
let firmware = [
    "x86_64",
    "/usr/share/OVMF/OVMF_CODE.fd",
    None,
    "/usr/share/OVMF/OVMF_VARS.fd",
    [];

    (* From RHEL 7.3, only secure boot variants of UEFI are shipped.
     * This requires additional qemu options, see RHBZ#1367615 for
     * details.
     *)
    "x86_64",
    "/usr/share/OVMF/OVMF_CODE.secboot.fd",
    None,
    "/usr/share/OVMF/OVMF_VARS.fd",
    [ "UEFI_FLAG_SECURE_BOOT_REQUIRED" ];

    "x86_64",
    "/usr/share/edk2/ovmf/OVMF_CODE.fd",
    None,
    "/usr/share/edk2/ovmf/OVMF_VARS.fd",
    [];

    "x86_64",
    "/usr/share/edk2/ovmf/OVMF_CODE.secboot.fd",
    None,
    "/usr/share/edk2/ovmf/OVMF_VARS.fd",
    [ "UEFI_FLAG_SECURE_BOOT_REQUIRED" ];

    "x86_64",
    "/usr/share/qemu/ovmf-x86_64-code.bin",
    None,
    "/usr/share/qemu/ovmf-x86_64-vars.bin",
    [];

    "aarch64",
    "/usr/share/AAVMF/AAVMF_CODE.fd",
    Some "/usr/share/AAVMF/AAVMF_CODE.verbose.fd",
    "/usr/share/AAVMF/AAVMF_VARS.fd",
    [];

    "aarch64",
    "/usr/share/edk2/aarch64/QEMU_EFI-pflash.raw",
    None,
    "/usr/share/edk2/aarch64/vars-template-pflash.raw",
    [];
]

let arches =
  List.sort_uniq compare (List.map (fun (arch, _, _, _, _) -> arch) firmware)

let generate_uefi_c () =
  generate_header CStyle LGPLv2plus;

  pr "#include <config.h>\n";
  pr "\n";
  pr "#include <stdio.h>\n";
  pr "\n";
  pr "#include \"guestfs.h\"\n";
  pr "#include \"guestfs-internal.h\"\n";

  List.iter (
    fun arch ->
      let firmware =
        List.filter (fun (arch', _, _, _, _) -> arch = arch') firmware in
      pr "\n";
      pr "struct uefi_firmware\n";
      pr "guestfs_int_uefi_%s_firmware[] = {\n" arch;
      List.iter (
        fun (_, code, code_debug, vars, flags) ->
          pr "  { \"%s\",\n" (c_quote code);
          (match code_debug with
           | None -> pr "    NULL,\n"
           | Some code_debug -> pr "    \"%s\",\n" (c_quote code_debug)
          );
          pr "    \"%s\",\n" (c_quote vars);
          pr "    %s\n" (if flags <> [] then String.concat "|" flags else "0");
          pr "  },\n";
      ) firmware;
      pr "};\n";
  ) arches
