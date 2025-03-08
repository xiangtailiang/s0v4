# 🦉 s0v4

What?!

s0v4 (or sova) is pronounced as сова in Russian. So, its an owl =)

This project is re-[reborn](https://github.com/fagci/uvk5-fagci-reborn) firmware for Quansheng UV-K5 radio, using FreeRTOS to make scan faster.

## Building

```sh
git submodule update --init --recursive --depth=1
make
```

## Flashing

```sh
k5prog -F -YYY -b ./bin/firmware.bin
```

