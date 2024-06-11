CC = sdcc

ODIR = obj

SRC = main.c
TARGET = $(ODIR)/8051-calculator

# rm command for Windows and Unix
ifeq ($(OS), Windows_NT)
	rm = del /f /q
	mkdir = if not exist $(ODIR) mkdir
else
	rm = rm -rf
	mkdir = mkdir -p $(ODIR)
endif

default: mkdir all

$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET).ihx
	packihx $(TARGET).ihx > $(TARGET).hex

.PHONY: mkdir
mkdir:
	$(mkdir) $(ODIR)

.PHONY: all
all: $(TARGET)

.PHONY: clean
clean:
	$(rm) $(ODIR)