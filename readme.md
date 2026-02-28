1. Use make clean/make to make the client
2. The usage for this client is:
./client <ipAddr> <portNumber> <directory>

3. <directory> should be "FilesFor5462
4. while it does take the ipAddr and portNumber, it does not use it. That portion of the logic is commented out.
6. Note: this makefile only makes the client, since there was no server in this lab, this lab submission will only contain:
- client.c
- Makefile
- FilesFor5462 ---> this submission will not have the CHUNKS subdirectory created or any chunks saved so that you can see it being made during your testing
- readme.md