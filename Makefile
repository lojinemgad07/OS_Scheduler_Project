POLICY_SOURCES ?=
build:
	gcc process_generator.c -o process_generator.out
	gcc clk.c -o clk.out
	gcc scheduler.c process_control.c $(POLICY_SOURCES) -o scheduler.out -lm
	gcc process.c -o process.out
	gcc test_generator.c -o test_generator.out

clean:
	rm -f *.out processes.txt resources.txt scheduler.log scheduler.perf

all: clean build

run:
	./process_generator.out


