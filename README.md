# Operating Systems project

This repository contains the implementation of a train scheduling solution, for the lab project of Operating Systems class at UNIFI.

## Description

The goal of the project is to simulate the behavior of 5 trains, that have to cross different railway segments. 
The goal of each train is to reach a pre-defined station, while their main constraint is that each railway segment can be occupied by at most one train at a time.

During its travel, a train is granted access to the next railway segment. The project deals with the implementation of two different ways to ask and grant access to railway segments, by using the C language and its built-in threading system.

## Usage

In order to run the project, you need to clone this repository and compile the `main.c` file, using a C language compiler like [GCC](https://gcc.gnu.org/).
Compiling the project is as simple as running:

```bash
gcc -o main main.c
```

Then, you can simply execute the resulting file like the following:

```bash
./main <arg>
```

Here, `<arg>` could be:

* ETCS1
* ETCS2 
* ETCS2 RBC

To see more details about each and every running mode, please have a look at the [assignment](assignment.pdf) and the related [report](report.pdf), but beware that they have been written in the Italian language.
