# monolith

The monolith application.


Check out the `docs` repo for information on monolith

## Contributing

1) Run compile-time test
2) Run all test scripts
3) Ensure code is formatted using `clang-format`

```
find . -regex '.*\.\(cpp\|hpp\)' -exec clang-format -style=file -i {} \;
```