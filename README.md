WinFlux: File Systems in Microsoft Windows Userspace – Challenging the stereotype!
==================================================================================

Implemented as a Bachelors project in the year 2010, WinFlux demonstrates that writing File Systems in Microsoft Windows in Userspace is easy. The motivation for this idea is the fact that file systems in Windows are integrated into the Kernel. Since implementing a traditional file system is extremely complex and difficult, presenting information seamlessly through files and folders has typically been limited to a small set of select programmers-often kernel experts who develop at the lowest layers of a system. WinFlux aims at providing an impetus to Windows software development community to inculcate support for various File Systems natively in Windows without any kernel level coding.

This it does by demonstrating a simple alternative to use the concept of file systems in user space. WinFlux uses Dokan Library (http://dokan-dev.net/en/) to help it do this. In short, Dokan is similar to FUSE but works on Windows. It is capable of trapping the filesystem interrupts and passing them onto the Callback functions which are implemented by a user application.

WinFlux preserves the ideology of inodes in Linux File Systems by implementing the file system data structures (inodes, superblock, etc) along with their standard operations in the user-space. Thus it demonstrates that any type of data structure can be implemented in user space; irrespective of the ones defined in the kernel. 
