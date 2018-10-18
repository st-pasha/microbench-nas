# NAs: sentinel values or bitmasks?

This benchmark compares the speed of two different ways for encoding
NA values: the **sentinel** method, and the **bitmask** method.

## The sentinel method

With this method, one particular value among the type's domain is denounced
to be "the NA" value. For example, for `int32` it's the smallest negative
value: `-2147483648`; for `int64` it's similarly `-9223372036854775808`.
For `double`s and `float`s the IEEE-754 standard offers a whole range of
values that can be used as NAs. For strings, a null value can be represented
as a null pointer. And so on.

The benefits of this method are:
- Optionally-NA values can be passed around the system as-is, without boxing.
  For example, a function may return an `int32_t` value, which can also be a
  null. On the other hand, with bitmask method the value would have been a
  `struct { int32_t; bool; }`, which actually takes twice as must storage.
- The NAs do not occupy any extra memory. Admittedly, most values are stored
  as 32 or 64 bits, so the extra memory cast of the bitmask method is around
  1.5% - 3%, a negligible amount. And if the vector has no NAs, that vector
  doesn't have to be allocated at all.
- Determining whether a particular value is NA or not is a simple comparison,
  as opposed to complex bit arithmetics used in bitmask method.
- Certain operations may be faster with this method:
  - appending two vectors can be done as-is, without the need to shift the
    bits in the second vector;
  - arithmetics on floating point values are simpler, since the NA values
    are handled by the CPU.

## The bitmask method

This method allocates an extra bitmask array of size `n / 8` bytes, where
each bit indicates whether the corresponding value in the main array is NA
or not. If the bit is 1, then the value is "valid". If the bit is 0, then
the value is considered NA, and the actual quantity stored in the main
array can be anything.

The benefits of this method are:
- It is universal: NA handling for all types is exactly the same. If a new
  type is added there is no need to pick any sentinel value.
- NA handling is uniform across integer and real types.
- For certain types picking a good sentinel is an impossible task. For
  example, `byte`: all values in the range 0 .. 255 are useful.
- Certain operations may be faster with this method:
  - adding two vectors `x + y`, the values can be added as-is, and then the
    bitmasks simply AND-ed together (64 values at a time!);
  - type casts are simpler: the NA bitmask doesn't change at all, and all
    other values can be converted as-is.


# Benchmark

We first attempt to replicate the results
[published by Wes McKinney](http://wesmckinney.com/blog/bitmaps-vs-sentinel-values/)
in his blog. The methodology is similar:
  - First, create a vector of `n` random integers in the range -10 .. 10;
  - Randomly replace a proportion `p` of them with NAs;
  - Main task: find the sum of all non-NA values in this vector;
  - Run the main task 100 times and report the average time;
  - Use different methods to run the main task, and compare timings across
    all methods.


# Conclusions


