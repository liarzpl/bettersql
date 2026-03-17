# Contributing to BetterSql

Thank you for your interest in contributing to **BetterSql**! As a modern C++17 library, we maintain high standards for code quality and safety.

## How to Contribute

### 1. Reporting Issues

- Use the **GitHub Issue Tracker**.
- Provide a minimal reproducible example (MCVE).
- Include compiler version and OS details.

### 2. Pull Requests

- Base your branch on `main`.
- **Header-Only Rule**: Ensure all logic remains within `include/bettersql.hpp`.
- **Standard**: Follow C++17 best practices. Avoid raw pointers; use RAII and smart pointers.
- **Style**: We follow the LLVM coding style. Please run `clang-format` before committing.
- **Documentation**: Update `README.md` if you add or change public APIs.

### 3. Testing

If you add a feature, please include a test case in the `tests/` directory to ensure stability.

## Development Workflow

1. Fork the repo.
2. Create a feature branch (`git checkout -b feature/amazing-feature`).
3. Commit your changes (`git commit -m 'Add amazing feature'`).
4. Push to the branch (`git push origin feature/amazing-feature`).
5. Open a Pull Request.

By contributing, you agree that your code will be licensed under the **MIT License**.
