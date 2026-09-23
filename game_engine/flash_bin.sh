#!/usr/bin/bash
avrdude -c usbasp -p m32 -U flash:w:"$1":r
