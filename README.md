# NAs: sentinel values or bitmasks?

This benchmark compares the speed of two different ways for encoding
NA values: the **sentinel** method, where each NA is stored
as its own dedicated value, or a **bitmask** method, where
the NAs are encoded using a separate bitmask.



See also: https://github.com/wesm/bitmaps-vs-sentinels
