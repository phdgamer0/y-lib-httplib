# HTTP / HTTPS Client and Server for y-- (Ymm)

Fast, lightweight HTTP client (GET, POST, PUT, DELETE) and HTTP/HTTPS server bindings for y-- powered by cpp-httplib.

## Installation

This library is an official external module for the **y--** (**Ymm**) programming language. You can install it effortlessly via **`yenv`**:

```bash
yenv install httplib
```

This will automatically clone the repository, build the shared library (`liby-httplib.so` / `y-httplib.dll`), and register it in `~/.yenv/lib/`.

## Usage

```ymm
import Http
from Http import *

define function main(): {
    res = Get("http://example.com", "/")
    if res != none: {
        print("Status: " + string(res.status))
        print("Body: " + res.body)
    }
}
```

## Documentation

Full documentation, including manual pages and reference texts, are available in the [`docs/`](docs/) directory:
- `httplib.man`: UNIX troff man page
- `httplib.txt`: Plaintext documentation reference

You can view the documentation using:
```bash
man -l docs/httplib.man
```

## License

This project is licensed under the same license as the **y--** programming language. See the [LICENSE](LICENSE) file for details.
