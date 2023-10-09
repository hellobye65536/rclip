# rclip
A very simple (<300 lines of C code) remote clipboard server for Linux using TCP, designed for use with netcat or ncat.

## Usage
```
Usage rclip [options] <copy port> <paste port>

Arguments:
  <copy port>                    when connecting to this port, send the new clipboard
  <paste port>                   when connecting to this port, recieve the current clipboard

Options:
  -h, --help                     display this help
  -c, --copy                     when copying, use this command instead of wl-clipboard/xclip
  -p, --paste                    when pasting, use this command instead of wl-clipboard/xclip
  -a, --address <ipv4 address>   bind to this address instead of all addresses
```

## Usage with netcat/ncat
Running rclip
```bash
rclip 1234 1235
```

Using netcat (BSD)
```bash
# copy
echo text | nc -N localhost 1234
# paste
nc -d localhost 1235 > out.txt
```

Using ncat (nmap)
```bash
# copy
echo text | ncat --send-only localhost 1234
# paste
nc --recv-only localhost 1235 > out.txt
```
