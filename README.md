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

Running with `n = 100,000,000` and various levels of `p`, the results are as follows:

| method                     | p = 0.0   | p = 0.01  | p = 0.1   | p = 0.5   |
|----------------------------|-----------|-----------|-----------|-----------|
| sum_ignore_nulls           | 0.0326502 | 0.0326378 | 0.0325075 | 0.0323759 |
| sum_ignore_nulls_batched   | 0.0350087 | 0.0346070 | 0.0343797 | 0.0345849 |
| sum_sentinel_nulls_if      | 0.1960710 | 0.1906290 | 0.1791320 | 0.3192560 |
| sum_sentinel_nulls_mul     | 0.0528372 | 0.0507768 | 0.0505761 | 0.0498112 |
| sum_sentinel_nulls_batched | 0.0519114 | 0.0500983 | 0.0495986 | 0.0494138 |
| sum_bitmask_nulls          | 0.1094530 | 0.1054360 | 0.1048650 | 0.1054050 |
| sum_bitmask_nulls_batched  | 0.0534536 | 0.0527309 | 0.0467189 | 0.0502715 |
| sum_bitmask_nulls_shortcut | 0.0379654 | 0.0396888 | 0.0757705 | 0.0529571 |
| sum_sentinel_nulls_omp1    | 0.0634438 | 0.0522847 | 0.0523712 | 0.0510477 |
| sum_sentinel_nulls_omp2    | 0.0283411 | 0.0235999 | 0.0226418 | 0.0229806 |
| sum_bitmask_nulls_omp2     | 0.0263455 | 0.0237212 | 0.0230295 | 0.0227701 |

The descriptions of various methods are best seen in the 
[code itself](https://github.com/st-pasha/microbench-nas/blob/master/nas.cc).
At high level, the methods are:
- *sum_ignore_nulls* - "fake" sum, where NA-ness of values is simply ignored.
  This should be the lower bound for other comparable methods.
- *sum_ignore_nulls_batched* - same, but the computation loop is unrolled.
- *sum_sentinel_nulls_if* - compute the sum using the sentinel values; the 
  inner loop uses `if` statement to determine whether a value should be 
  used for sum calculation.
- *sum_sentinel_nulls_mul* - same, but the NA values are eliminated from the
  sum via multiplying by `(val != NA)`.
- *sum_sentinel_nulls_batched* - same as before, but unroll inner loop.
- *sum_bitmask_nulls* - the simplest implementation of computing the sum 
  using the validity bitmask.
- *sum_bitmask_nulls_batched* - same as previous, but the inner loop is 
  unrolled.
- *sum_bitmask_nulls_shortcut* - same as previous, but first check whether
  there are any NAs inside the 8-element chunk, and if so compute using a
  simpler formula.
- *sum_sentinel_nulls_omp1* - multi-threaded variant of 
  *sum_sentinel_nulls_mul*, the iterations run interleaved.
- *sum_sentinel_nulls_omp2* - similar to the previous, but the result
  is computed using OMP's built-in `reduce` clause.
- *sum_bitmask_nulls_omp2* - same as previous, but use the validity bitmask
  instead of the sentinel values.


## Conclusions

- For sentinel method, unrolling the inner loop provides no measurable benefit.
- At the same time, for bitmasks unrolled loop is about twice faster than the
  simple one.
- For sentinel method, using multiplication instead of the if condition is
  important, as it provides x2 - x6 performance boost.
- For bitmask method, short-cutting the calculations when validity byte is full
  is beneficial when the number of NAs is sufficiently small. However in other
  cases this approach leads to worse timings.
- The "batched" implementations of sentinel and bitmask methods have the same
  timing (any reported difference is statistically insignificant).
- At the same time, the bitmask method can be somewhat faster when the number
  of NAs is known to be small.
- When performing the calculations in-parallel, the same results hold: both
  methods exhibit the same performance.
