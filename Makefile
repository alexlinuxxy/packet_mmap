CFLAGS := -Wall -O2
TARGET := sender receiver
all: $(TARGET)

sender: sender.o
	gcc $(CFLAGS) -o $@ $^

receiver: receiver.o
	gcc $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET) *.o
