PIB_APP := ../../../firmware/App
OUT := build

CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wdouble-promotion \
          -fsanitize=address,undefined -fno-omit-frame-pointer \
          -Istubs -I../Core/Inc -I$(PIB_APP)/Inc

SRCS := test_host.c ../Core/Src/spi_telemetry_test.c $(PIB_APP)/Src/telemetry.c

$(OUT)/test_host: $(SRCS) stubs/stm32g4xx_hal.h stubs/main.h ../Core/Inc/spi_telemetry_test.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(SRCS) -lm -o $@

test: $(OUT)/test_host
	./$(OUT)/test_host

clean:
	rm -rf $(OUT)

.PHONY: test clean
