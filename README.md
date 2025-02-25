## Building

```sh
git submodule update --init --recursive --depth=1
make
```

## Flashing

```sh
k5prog -F -YYY -b ./bin/firmware.bin
```

