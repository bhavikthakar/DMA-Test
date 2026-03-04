CC=gcc
CFLAGS=-Wall -Wextra -g -I.
UNIT_TEST_BIN=unit_test_dma
INTERACTIVE_BIN=test_dma_api_interactive

all: $(UNIT_TEST_BIN) $(INTERACTIVE_BIN)

# Unit test binary (automated)
$(UNIT_TEST_BIN): unit_test_dma.c dma_api.c fw_log.c
	$(CC) $(CFLAGS) -o $@ unit_test_dma.c dma_api.c fw_log.c -pthread

# Interactive test binary
$(INTERACTIVE_BIN): test_dma_api.c dma_api.c fw_log.c
	$(CC) $(CFLAGS) -o $@ test_dma_api.c dma_api.c fw_log.c -pthread

# Run unit tests
test: $(UNIT_TEST_BIN)
	./$(UNIT_TEST_BIN)

# Run interactive test
interactive: $(INTERACTIVE_BIN)
	./$(INTERACTIVE_BIN)

clean:
	rm -f $(UNIT_TEST_BIN) $(INTERACTIVE_BIN)