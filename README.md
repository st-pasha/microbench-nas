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

This method has its benefits:
- No need to al

And drawbacks:
- It is not universal: for each new type the NA value has to be carefully
  picked. What if there is a type for which no good sentinel exists?
- 


# Conclusions



See also: https://github.com/wesm/bitmaps-vs-sentinels
