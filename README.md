# G-Parallel-Profiler

A parallel profiler that uses hardware performance counter to provide detailed
function usage in each thread.

## Example Output

```
Profiler output:
  thread 0:
    func1 800 cycles 80%
    func2 100 cycles 10%
    func3 50  cycles 5%
    func4 50  cycles 5%
  thread 1:
    func1 500 cycles 50%
    func2 200 cycles 20%
    func3 150 cycles 15%
    func4 150 cycles 15%
  ...
```

## Example Usage

__IMPORTANT__: Remember to build your program with `-g` option which turns on
the debug information.

```
cd test
make clean all
cd ..
make clean all

# Running a test program that uses four threads to do some random computation
./g-profiler test/test

# Running a password cracker program that cracks passwords in multiple threads
./g-profiler test/password-cracker test/passwords.txt
```



