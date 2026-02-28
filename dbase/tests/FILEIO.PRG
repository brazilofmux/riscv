h = FCREATE("testfile.txt")
if h > 0
    ? "File created, handle =", h
    n = FWRITE(h, "Hello Low-Level I/O world!")
    ? "Bytes written =", n
    ? "FSEEK to start:", FSEEK(h, 0, 0)
    s = FREAD(h, 5)
    ? "Read first 5 bytes:", s
    ? "FCLOSE:", FCLOSE(h)
else
    ? "Error creating file, FERROR =", FERROR()
endif

? "--- Test FOPEN ---"
h2 = FOPEN("testfile.txt", 0)
if h2 > 0
    ? "File opened for read, handle =", h2
    ? "FSEEK to end:", FSEEK(h2, 0, 2)
    ? "File size (tell):", FSEEK(h2, 0, 1)
    ? "FCLOSE:", FCLOSE(h2)
else
    ? "Error opening file"
endif
QUIT
