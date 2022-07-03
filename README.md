archeolog (short for "archeologist") is a program for extracting data from large log files with a fast seeking algorithm.
It uses binary-search algorithm with a small block size to quickly find the first line with a user-specified timestamp,
 then it uses zero-copy data block algorithms to filter the output data.
File-reading uses aligned offsets and an aligned memory buffer, doesn't read file blocks twice.
Kernel-userspace data transfer is the only place where data is copied.
The architecture allows to extend archeolog with additional functions such as asynchronous parallel file reading (plus read-ahead), text filtering, etc (these functions are NOT implemented yet).

## Build

	git clone --depth=1 https://github.com/stsaz/ffbase
	git clone --depth=1 https://github.com/stsaz/ffos
	git clone --depth=1 https://github.com/stsaz/archeolog
	cd archeolog
	make -j8

## Example

This command outputs all lines between 08:00 and 09:00:

	archeolog -s '2022-06-26 08:00:00' -e '2022-06-26 09:00:00' large-file.log

## License

Absolutely free.
