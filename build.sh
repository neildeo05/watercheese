#!/bin/bash

clang -o watercheese -O2 -framework Hypervisor -mmacosx-version-min=11.0 main.c
codesign --entitlements watercheese.entitlements --force -s - watercheese
codesign -d --entitlements :- ./watercheese
