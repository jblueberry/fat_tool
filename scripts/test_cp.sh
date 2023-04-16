#!/bin/bash

./scripts/build.sh
./scripts/copy_img.sh
./scripts/check.sh
./build/fat fat32.img cp local:./README.md image:/blue/blue1/READMEfsfaafasfasfasfafas.md
./scripts/check.sh