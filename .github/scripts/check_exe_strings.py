#!/usr/bin/env python3
# Copyright (C) 2026 Muertazos Racing.
# Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Verificacion estatica de un ejecutable: no debe contener ciertas cadenas.

Uso: check_exe_strings.py EXE --forbid CADENA [CADENA...] --require CADENA [CADENA...]

Busca cada cadena, sin distinguir mayusculas, en ASCII y en UTF-16LE. Falla si:
  - EXE no existe o esta vacio;
  - el buscador no detecta una cadena conocida en ninguna de las dos codificaciones
    (autoprueba con datos sinteticos), o
  - alguna cadena de --require no esta en EXE (prueba de que el escaneo no es vacuo), o
  - alguna cadena de --forbid aparece en EXE.
"""

import argparse
import os
import sys


def encodings(text):
    return {"ASCII": text.lower().encode("ascii"), "UTF-16LE": text.lower().encode("utf-16-le")}


def find_hits(data, text):
    """Codificaciones en las que `text` aparece en `data` (sin distinguir mayusculas ASCII)."""
    lowered = data.lower()
    return [name for name, needle in encodings(text).items() if needle in lowered]


def self_test():
    sample = b"xx" + "MuertazosRacing.com".encode("ascii") + b"yy" + "BeamMP.GG".encode("utf-16-le") + b"zz"
    if find_hits(sample, "muertazosracing.com") != ["ASCII"]:
        return "la autoprueba no detecta una cadena ASCII"
    if find_hits(sample, "beammp.gg") != ["UTF-16LE"]:
        return "la autoprueba no detecta una cadena UTF-16LE"
    if find_hits(sample, "beammp.com"):
        return "la autoprueba da un falso positivo"
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("exe")
    parser.add_argument("--forbid", nargs="+", required=True)
    parser.add_argument("--require", nargs="+", required=True)
    args = parser.parse_args()

    problem = self_test()
    if problem:
        print("FALLO: " + problem)
        return 1
    if not os.path.isfile(args.exe):
        print("FALLO: no existe " + args.exe)
        return 1
    size = os.path.getsize(args.exe)
    if size == 0:
        print("FALLO: " + args.exe + " esta vacio")
        return 1
    with open(args.exe, "rb") as handle:
        data = handle.read()
    print("escaneando %s (%d bytes)" % (args.exe, size))

    failed = False
    for text in args.require:
        hits = find_hits(data, text)
        print("  requerida  %-24s %s" % (text, ",".join(hits) if hits else "NO ENCONTRADA"))
        failed = failed or not hits
    for text in args.forbid:
        hits = find_hits(data, text)
        print("  prohibida  %-24s %s" % (text, ",".join(hits) if hits else "ausente"))
        failed = failed or bool(hits)
    if failed:
        print("FALLO: verificacion estatica")
        return 1
    print("OK: sin cadenas prohibidas y con las requeridas")
    return 0


if __name__ == "__main__":
    sys.exit(main())
