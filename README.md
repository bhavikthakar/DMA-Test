## Building

### Build all targets
```bash
make all
```

### Build just unit tests
```bash
make unit_test_dma
```

### Build just interactive tests
```bash
make test_dma_api_interactive
```

### Run unit tests
```bash
make test
```

### Run interactive tests
```bash
make interactive
```

### Clean binaries
```bash
make clean

I would ask the RTL team to provide more detailed status register information when a DMA error interrupt is generated, such as:

(1) DMA Internal Error – for example, caused by a buffer length of 0.

(2) Source or destination alignment issue indication.

(3) AXI/NoC bus error at a specified address.

(4) Scatter-Gather (SG) mode descriptor address pointer error.

(5) Most importantly, when a failure occurs in SG mode, whether it indicates which specific descriptor caused the failure.
