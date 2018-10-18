# NAs: sentinel values or bitmasks?

This benchmark compares the speed of two different ways for encoding
    NA values: the <strong>sentinel</strong> method, where each NA is stored
    as its own dedicated value, or a <strong>bitmask</strong> method, where
    the NAs are encoded using a separate bitmask.

