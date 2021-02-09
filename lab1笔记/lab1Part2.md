#### Part 2: The Boot Loader

​		对于PC来说，软盘，硬盘都可以被划分为一个个大小为512字节的区域，叫做扇区。一个扇区是一次磁盘操作的最小粒度。每一次读取或者写入操作都必须是一个或多个扇区。如果一个磁盘是可以被用来启动操作系统的，就把这个磁盘的第一个扇区叫做启动扇区。当BIOS找到一个可以启动的软盘或硬盘后，它就会把这512字节的启动扇区加载到内存地址0x7c00~0x7dff这个区域内。然后使用jmp指令将CS:IP设置为0000:7c00，将控制权传递给引导加载程序。就像BIOS的加载地址一样，这些地址是相当随意的——但是对于pc来说，它们是固定的和标准化的。

​        对于6.828，我们将采用传统的硬盘启动机制，这就意味着我们的boot loader程序的大小必须小于512字节。整个boot loader是由一个汇编文件，boot/boot.S，以及一个C语言文件，boot/main.c组成。仔细查看这些源文件，确保你理解了所发生的事情。Boot loader必须完成两个主要的功能。

1. 首先，boot loader要把处理器从实模式转换为32bit的保护模式，因为只有在这种模式下软件可以访问超过1MB空间的内容。
2. 其次，引导加载程序通过x86的特殊I/O指令直接访问IDE磁盘设备寄存器，从硬盘中读取内核。

​        在您理解引导加载程序的源代码之后，请查看obj/boot/boot.asm文件，这个文件是GNUmakefile在编译引导加载程序后创建的引导加载程序的反汇编文件。这个反汇编文件可以很容易地查看所有引导加载程序代码驻留在物理内存中的位置，并且可以更容易地通过GDB逐步跟踪执行引导加载程序时所发生的事情。同样的，obj/kern /kernel.asm包含对JOS内核的反汇编，这对于调试通常很有用。

​       使用b命令在GDB中设置地址断点。例如，b *0x7c00在地址0x7c00处设置了一个断点。一旦到了断点，您就可以使用c和si命令继续执行:c使QEMU继续执行，直到下一个断点(或者直到您在GDB中按下Ctrl-C)，并且si N一次执行N个指令。

​       要查看内存中的指令(除了马上要执行的、由GDB自动打印的指令之外)，可以使用x/i命令。这个命令的语法是x/Ni ADDR，其中N是要反汇编的连续指令的数量，ADDR是开始反汇编的内存地址。

**Exercise 3.** 看一看[lab tools guide](https://pdos.csail.mit.edu/6.828/2018/labguide.html)，特别是关于GDB命令的部分。即使您熟悉GDB，这也包括一些对操作系统工作非常有用的GDB命令。

​		在地址0x7c00处设置一个断点，这是引导扇区加载到位置。继续执行，直到断点。跟踪boot/boot.S中的代码。使用源代码和反汇编文件obj/boot/boot.asm跟踪代码。还可以使用GDB中的x/i命令来查看引导加载程序中的反汇编指令序列，并将原始引导加载程序源代码与obj/boot/boot.asm中的反汇编代码进行比较。

​        追踪到bootmain函数中，而且还要具体追踪到readsect()子函数里面。找出和readsect()c语言程序的每一条语句所对应的汇编指令，回到bootmain()，然后找出把内核文件从磁盘读取到内存的那个for循环所对应的汇编语句。找出当循环结束后会执行哪条语句，在那里设置断点，继续运行到断点，然后运行完所有的剩下的语句。

​       答：下面分析一下这道练习中所涉及到的两个重要文件，它们一起组成了boot loader。分别是/boot/boot.S和/boot/main.c文件。其中前者是一个汇编文件，后者是一个C语言文件。当BIOS运行完成之后，CPU的控制权就会转移到boot.S文件上。所以我们首先看一下boot.S文件。

```
.globl start
 13 start:
 14   .code16                     # Assemble for 16-bit mode
 15   cli                         # Disable interrupts
 16   cld                         # String operations increment
 17 
 18   # Set up the important data segment registers (DS, ES, SS).
 19   xorw    %ax,%ax             # Segment number zero
 20   movw    %ax,%ds             # -> Data Segment
 21   movw    %ax,%es             # -> Extra Segment
 22   movw    %ax,%ss             # -> Stack Segment
```

​        这几条指令就是boot.S最开始的几句，其中cli是boot loader的第一条指令。这条指令用于把所有的中断都关闭。因为在BIOS运行期间有可能打开了中断。此时CPU工作在实模式下。cld这条指令用于指定之后发生的串处理操作的指针移动方向。3个段寄存器，ds，es，ss全部清零，因为经历了BIOS，操作系统不能保证这三个寄存器中存放的是什么数，这也是为后面进入保护模式做准备。

```
 24   # Enable A20:
 25   #   For backwards compatibility with the earliest PCs, physical
 26   #   address line 20 is tied low, so that addresses higher than
 27   #   1MB wrap around to zero by default.  This code undoes this.
 28   seta20.1:
 29   inb     $0x64,%al               # Wait for not busy
 30   testb   $0x2,%al
 31   jnz     seta20.1
 32 
 33   movb    $0xd1,%al               # 0xd1 -> port 0x64
 34   outb    %al,$0x64
 35 
 36   seta20.2:
 37   inb     $0x64,%al               # Wait for not busy
 38   testb   $0x2,%al
 39   jnz     seta20.2
 40 
 41   movb    $0xdf,%al               # 0xdf -> port 0x60
 42   outb    %al,$0x60
```

```inb     $0x64,%al```  将编号为0x64的I/O端口读入寄存器al(8位)，```testb $0x2, %al ```表示$0x2与%al进行and操作，但是与and操作不同，testb不改变原操作数，并根据结果设置标志寄存器，如果al中第2个bit为1时，```jnz seta20.2```将会跳转，而al中第2个bit为1，表示缓冲区满了，I/O端口还没有取走数据，这时不能再向端口传送数据。指令是在不断的检测bit1。bit1的值代表输入缓冲区是否满了，也就是说CPU传送给控制器的数据是否已经取走了。

​		当0x64端口准备好读入数据后，现在就可以写入数据了，所以33~34这两条指令是把0xd1这条数据写入到0x64端口中。当向0x64端口写入数据时，则代表向键盘控制器804x发送指令。这个指令将会被送给0x60端口。

![img](https://img-blog.csdn.net/20160519133615444)

通过图中可见，D1指令代表下一次写入0x60端口的数据将被写入给804x控制器的输出端口。可以理解为下一个写入0x60端口的数据是一个控制指令。将d1这条数据写入到0x64端口时，同时也表示d1为一个指令，代表向键盘控制器804x发送指令。这个指令将会被送给0x60端口。36-39行指令又在判断D1是否被取走，以便将再写入数据。

41-42行会向0x60端口控制器输入新的指令0xdf。通过查询我们看到0xDF指令的含义如下：

![img](https://img-blog.csdn.net/20160519133802733)

这个指令的含义可以从图中看到，使能A20线，代表可以进入保护模式了。

```
 44   # Switch from real to protected mode, using a bootstrap GDT
 45   # and segment translation that makes virtual addresses 
 46   # identical to their physical addresses, so that the 
 47   # effective memory map does not change during the switch.
 48   lgdt    gdtdesc
 49   movl    %cr0, %eax
 50   orl     $CR0_PE_ON, %eax
 51   movl    %eax, %cr0
```

​      ```lgdt gdtdesc```是把gdtdesc这个标识符的值送入全局映射描述符表寄存器GDTR中。这个GDT表是处理器工作于保护模式下一个非常重要的表。至于这条指令的功能就是把关于GDT表的一些重要信息存放到CPU的GDTR寄存器中，其中包括GDT表的内存起始地址，以及GDT表的长度。这个寄存器由48位组成，其中低16位表示该表长度，高32位表示该表在内存中的起始地址。所以gdtdesc是一个标识符，标识着一个内存地址。从这个内存地址开始之后的6个字节中存放着GDT表的长度和起始地址。我们可以在这个文件的末尾看到gdtdesc，如下：

```
 75 # Bootstrap GDT
 76 .p2align 2                                # force 4 byte alignment
 77 gdt:
 78   SEG_NULL              # null seg
 79   SEG(STA_X|STA_R, 0x0, 0xffffffff) # code seg
 80   SEG(STA_W, 0x0, 0xffffffff)           # data seg
 81 
 82 gdtdesc:
 83   .word   0x17                            # sizeof(gdt) - 1
 84   .long   gdt                             # address gdt
```

  		77行gdt是一个标识符，标识从这里开始就是GDT表了。可见这个GDT表中包括三个表项(4,5,6行)，分别代表三个段，null seg，code seg，data seg。由于JOS其实并没有使用分段机制，也就是说数据和代码都是写在一起的，所以数据段和代码段的起始地址都是0x0，大小都是0xffffffff=4GB。

​        在第78~80行是调用SEG()子程序来构造GDT表项的。这个子函数定义在mmu.h中，形式如下：

```c
#define SEG(type,base,lim)                    \
                 .word (((lim) >> 12) & 0xffff), ((base) & 0xffff);    \
                 .byte (((base) >> 16) & 0xff), (0x90 | (type)),        \
                 (0xC0 | (((lim) >> 28) & 0xf)), (((base) >> 24) & 0xff)
```

可见函数需要3个参数，一是type即这个段的访问权限，二是base，这个段的起始地址，三是lim，即这个段的大小界限。gdt表中的每一个表项的结构如图所示：

![image-20201214005839526](C:\Users\2019310661\AppData\Roaming\Typora\typora-user-images\image-20201214005839526.png)

![这里写图片描述](https://img-blog.csdn.net/20160519134331048)

 每个表项一共8字节，其中limit_low就是limit的低16位。base_low就是base的低16位，依次类推。
 然后在gdtdesc处就要存放这个GDT表的信息了，其中0x17是这个表的大小24-1 = 0x17 = 23，紧接着就是这个表的起始地址gdt。

```
 44   # Switch from real to protected mode, using a bootstrap GDT
 45   # and segment translation that makes virtual addresses 
 46   # identical to their physical addresses, so that the 
 47   # effective memory map does not change during the switch.
 48   lgdt    gdtdesc
 49   movl    %cr0, %eax
 50   orl     $CR0_PE_ON, %eax
 51   movl    %eax, %cr0
```

　当加载完GDT表的信息到GDTR寄存器之后。紧跟着3个操作，49~51指令。 这几步操作是在修改CR0寄存器的内容。CR0寄存器还有CR1~CR3寄存器都是80x86的控制寄存器。其中$CR0_PE的值定义于”mmu.h”文件中，为0x00000001。可见上面的操作是把CR0寄存器的bit0置1，CR0寄存器的bit0是保护模式启动位，把这一位置1代表保护模式启动。

```
ljmp    $PROT_MODE_CSEG, $protcseg
```

这只是一个简单的跳转指令，这条指令的目的在于把当前的运行模式切换成32位地址模式。

```ljmp $section,$offset```这里$section和offset表示的就是，以section为段地址，offset为段内偏移地址

```
8 .set PROT_MODE_CSEG, 0x8         # kernel code segment selector
9 .set PROT_MODE_DSEG, 0x10        # kernel data segment selector
```

与78行的GDT表第二项、第三项对应。

```
 58 protcseg:
 59   # Set up the protected-mode data segment registers
 60   movw    $PROT_MODE_DSEG, %ax    # Our data segment selector
 61   movw    %ax, %ds                # -> DS: Data Segment
 62   movw    %ax, %es                # -> ES: Extra Segment
 63   movw    %ax, %fs                # -> FS
 64   movw    %ax, %gs                # -> GS
 65   movw    %ax, %ss                # -> SS: Stack Segment
```

 修改这些寄存器的值，这些寄存器都是段寄存器。因为现在进入32位之后，寻址采用GDT表了，GDT表第三项对应数据段，所以将所有数据段选择子付给这些段寄存器，一方面也是刚刚加载完GDTR寄存器我们必须要重新加载所有的段寄存器的值。但是CS段无法直接这样赋值，必须通过长跳转或call才能改变CS的值，

```
 67   # Set up the stack pointer and call into C.
 68   mov    $start, %esp
 69   call bootmain
```

接下来的指令就是要设置当前的esp寄存器的值，然后准备正式跳转到main.c文件中。其中start的地址表示0x7c00，栈是地址向下使用的。

```c
 38 void
 39 bootmain(void)
 40 {
 41     struct Proghdr *ph, *eph;
 42 
 43     // read 1st page off disk
 44     readseg((uint32_t) ELFHDR, SECTSIZE*8, 0);
 45 
 46     // is this a valid ELF?
 47     if (ELFHDR->e_magic != ELF_MAGIC)
 48         goto bad;
 49 
 50     // load each program segment (ignores ph flags)
 51     ph = (struct Proghdr *) ((uint8_t *) ELFHDR + ELFHDR->e_phoff);
 52     eph = ph + ELFHDR->e_phnum;
 53     for (; ph < eph; ph++)
 54         // p_pa is the load address of this segment (as well
 55         // as the physical address)
 56         readseg(ph->p_pa, ph->p_memsz, ph->p_offset);
 57 
 58     // call the entry point from the ELF header
 59     // note: does not return!
 60     ((void (*)(void)) (ELFHDR->e_entry))();
 61 
 62 bad:
 63     outw(0x8A00, 0x8A00);
 64     outw(0x8A00, 0x8E00);
 65     while (1)
 66         /* do nothing */;
 67 }
```

接下来分析一下这个函数的每一条指令：

```c
readseg((uint32_t) ELFHDR, SECTSIZE*8, 0);
```

 这里面调用了一个函数readseg，这个函数在bootmain之后被定义了：

```
void readseg(uchar *pa, uint count, uint offset);
```

　    它的功能从注释上来理解是，把距离内核起始地址offset个偏移量存储单元作为起始，将它和它之后的count字节的数据读出送入以pa为起始地址的内存物理地址处。

​		所以这条指令是把内核的第一个页(4KB = 4096 = SECTSIZE**8 = 512*8)的内容读取的内存地址ELFHDR(0x10000)处。其实完成这些后相当于把操作系统映像文件的elf头部读取出来放入内存中。

读取完这个内核的elf头部信息后，需要对这个elf头部信息进行验证，并且也需要通过它获取一些重要信息。所以有必要了解下elf头部。

```c
#define EI_NIDENT 16
typedef struct {
       unsigned char e_ident[EI_NIDENT];
       ELF32_Half e_type;
       ELF32_Half e_machine;
       ELF32_Word e_version;
       ELF32__Addr e_entry;
       ELF32_Off e_phoff;
       ELF32_Off e_shoff;
       ELF32_Word e_flags;
       ELF32_Half e_ehsize;
       ELF32_Half e_phentsize;
       ELF32_Half e_phnum;
       ELF32_Half e_shentsize;
       ELF32_Half e_shnum;
       ELF32_Half e_shstrndx;
}Elf32_Ehdr;
```

```
e_ident ： ELF的一些标识信息，前四位为.ELF,其他的信息比如大小端等
e_machine ： 文件的目标体系架构，比如ARM
e_version : 0为非法版本，1为当前版本
e_entry ： 程序入口的虚拟地址
e_phoff ： 程序头部表偏移地址
e_shoff ： 节区头部表偏移地址
e_flags ：保存与文件相关的，特定于处理器的标志
e_ehsize ：ELF头的大小
e_phentsize ： 每个程序头部表的大小
e_phnum ：程序头部表的数量
e_shentsize：每个节区头部表的大小
e_shnum ： 节区头部表的数量
e_shstrndx：节区字符串表位置
```

​			一个ELF格式的文件一般包括四个部分：ELF头表（Head Table），程序头表（Program Head Table），节头表（Section Head Table），节（Section）。也就是说：ELF文件中有三个“表“，其他的都是一个一个叫做”节“的东西。

​			ELF头表的作用就很显而易见了，它就是要告诉来客：我这个文件是ELF格式的，我能干啥（可执行文件，链接库还是可重定位文件），我需要用多少多少位的操作系统，你得用什么样的CPU架构来运行我，我是按什么字节序来存数据的等等。ELF保存了程序头表、节头表的位置与大小。程序头表只可以找到同类的节的聚集地，但是节头表可以细致的找到每一个节的位置。那有节头表就够了嘛，能找到每一个节的位置不就行了，要程序头表有啥用？这就跟ELF文件的两个阶段有关系了，其实节头表和程序头表分别是在不同阶段起作用的：链接时用到节头表，执行时用到程序头表。

![img](https://img2018.cnblogs.com/blog/1746865/201910/1746865-20191018133018165-294625133.png)

​        在链接的时候，需要用到节头表，执行的时候，需要用到程序头表。当我们站在操作系统装载可执行文件的角度看问题时,可以发现它实际上并不关心可执行文件各个节所包含的实际内容,操作系统只关心一些跟装载相关的问题，最主要的是节的权限(可读、可写、可执行)。在ELF格式文件中，权限的组合种类主要分为下面的这三种：①可读可执行（如代码）②可读可写（如数据）③只读。所以有一个很好的解决办法就是把性质相同的节（Section）“捆绑”到一起变成一个“段（Segment）”，段在保存的时候按页为单位（一个段里有若干个”节“，具体个数不一定），段内的各个节是首尾相接依次排放好的，这样做可以明显减少页面内部的碎片，起到节省内存空间的作用。

```
 46     // is this a valid ELF?
 47     if (ELFHDR->e_magic != ELF_MAGIC)
 48         goto bad;
```

 elf头部信息的magic字段是整个头部信息的开端。并且如果这个文件是格式是ELF格式的话，文件的elf->magic域应该是ELF_MAGIC的，所以这条语句就是判断这个输入文件是否是合法的elf可执行文件。

```
51     ph = (struct Proghdr *) ((uint8_t *) ELFHDR + ELFHDR->e_phoff);
51     ph = (struct Proghdr *) ((uint8_t *) ELFHDR + ELFHDR->e_phoff);
52     eph = ph + ELFHDR->e_phnum;
53     for (; ph < eph; ph++)
54         // p_pa is the load address of this segment (as well
55         // as the physical address)
56         readseg(ph->p_pa, ph->p_memsz, ph->p_offset);
57     
58     // call the entry point from the ELF header
59     // note: does not return!
60     ((void (*)(void)) (ELFHDR->e_entry))();

```

​        我们知道头部中一定包含Program Header Table。这个表格存放着程序中所有段的信息。通过这个表我们才能找到要执行的代码段，数据段等等。所以我们要先获得这个表。 这条指令就可以完成这一点，首先elf是表头起址，而phoff字段代表Program Header Table距离表头的偏移量。所以ph可以被指定为Program Header Table表头。

 由于phnum中存放的是Program Header Table表中表项的个数，即段的个数。所以这步操作是吧eph指向该表末尾。

​       这个for循环就是在加载所有段到内存中。ph->paddr根据参考文献中的说法指的是这个段在内存中的物理地址。ph->off字段指的是这段的开头相对于这个elf文件的开头的偏移量。ph->filesz字段指的是这个段在elf文件中的大小。ph->memsz则指的是这个段被实际装入内存后的大小。通常来说memsz一定大于等于filesz，因为段在文件中时许多未定义的变量并没有分配空间给它们。所以这个循环就是在把操作系统内核的各个段从外存读入内存中。

​        e_entry字段指向的是这个文件的执行入口地址。所以这里相当于开始运行这个文件。也就是内核文件。 自此就把控制权从boot loader转交给了操作系统的内核。

下面回答一下文中提出的四个问题：
1. 在什么时候处理器开始运行于32bit模式？到底是什么把CPU从16位切换为32位工作模式？

   答：在boot.S文件中，计算机首先工作于实模式，此时是16bit工作模式。当运行完 ” ljmp $PROT_MODE_CSEG, $protcseg ” 语句后，正式进入32位工作模式。根本原因是此时CPU工作在保护模式下。

　2. boot loader中执行的最后一条语句是什么？内核被加载到内存后执行的第一条语句又是什么？
　答：boot loader执行的最后一条语句是bootmain子程序中的最后一条语句 ” ((void (*)(void)) (ELFHDR->e_entry))(); “，即跳转到操作系统内核程序的起始指令处。
这个第一条指令位于/kern/entry.S文件中，第一句 movw $0x1234, 0x472

　3. 内核的第一条指令在哪里？
　答：第一条指令位于/kern/entry.S文件中。

　4. boot loader是如何知道它要读取多少个扇区才能把整个内核都送入内存的呢？在哪里找到这些信息？
　答：首先关于操作系统一共有多少个段，每个段又有多少个扇区的信息位于操作系统文件中的Program Header Table中。这个表中的每个表项分别对应操作系统的一个段。并且每个表项的内容包括这个段的大小，段起始地址偏移等等信息。所以如果我们能够找到这个表，那么就能够通过表项所提供的信息来确定内核占用多少个扇区。
那么关于这个表存放在哪里的信息，则是存放在操作系统内核映像文件的ELF头部信息中。

**Loading the Kernel**

现在我们将进一步研究引导加载程序boot/main.c中的C语言部分。但在此之前，我们回顾一下C编程的基础知识。

**Exercise 4.**    阅读关于C语言的指针部分的知识。最好的参考书自然是"The C Programming Language"。阅读5.1到5.5节。然后下载pointers.c的代码，并且编译运行它，确保你理解在屏幕上打印出来的所有的值是怎么来的。尤其要重点理解第1行，第6行的指针地址是如何得到的，以及在第2行到第4行的值是如何得到的，还有为什么在第5行打印出来的值看起来像程序崩溃了。

```c
#include <stdio.h>
#include <stdlib.h>

void
f(void)
{
    int a[4];
    int *b = malloc(16);
    int *c;
    int i;

    printf("1: a = %p, b = %p, c = %p\n", a, b, c);

    c = a;
    for (i = 0; i < 4; i++)
	a[i] = 100 + i;
    c[0] = 200;
    printf("2: a[0] = %d, a[1] = %d, a[2] = %d, a[3] = %d\n",
	   a[0], a[1], a[2], a[3]);

    c[1] = 300;
    *(c + 2) = 301;
    3[c] = 302;
    printf("3: a[0] = %d, a[1] = %d, a[2] = %d, a[3] = %d\n",
	   a[0], a[1], a[2], a[3]);

    c = c + 1;
    *c = 400;
    printf("4: a[0] = %d, a[1] = %d, a[2] = %d, a[3] = %d\n",
	   a[0], a[1], a[2], a[3]);

    c = (int *) ((char *) c + 1);
    *c = 500;
    printf("5: a[0] = %d, a[1] = %d, a[2] = %d, a[3] = %d\n",
	   a[0], a[1], a[2], a[3]);

    b = (int *) a + 1;
    c = (int *) ((char *) a + 1);
    printf("6: a = %p, b = %p, c = %p\n", a, b, c);
}

int
main(int ac, char **av)
{
    f();
    return 0;
}
```

​        要理解boot/main.c，你需要知道ELF二进制文件是什么。当编译和链接一个C程序(比如JOS内核)时，编译器将每个C源文件('. C ')转换成一个object文件('.o')，该object文件包含硬件认识的二进制格式编码的汇编指令。链接器然后将所有已编译的object文件组合成一个的二进制镜像，如obj/kern/kernel。该镜像文件是ELF格式的二进制文件，即“可执行和可链接格式(Executable and Linkable Format)”。

​		对于6.828这门课程，你可以把ELF可执行文件看作是一个带有加载信息的头文件，后面跟着几个程序段，每个程序段都是一个连续的代码块或数据块，每个程序段加载到指定的地址内存中。引导加载程序不会修改代码或数据；它将其加载到内存中并开始执行。

​		ELF二进制文件以固定长度的ELF表开始，然后是列出要加载的每个程序段的变长程序段表。ELF表的结构在inc/ ELF .h中定义。我们感兴趣的部分有:

- .text：程序的可执行指令。
- .rodata：只读数据，例如由C编译器生成的ASCII字符串常量。(不过，我们不会通过设置硬件来禁止写入.rodata。)
- data：部分保存着程序的初始化数据，比如用int x = 5;这样的初始化式声明的全局变量。

​        当链接器计算程序的内存分布大小时，它为未初始化的全局变量保留空间，例如int x;，在内存中紧接.data的.bss节中。C默认“未初始化的全局变量值为0，因此，ELF二进制文件不需要存储.bss的内容。链接器只记录.bss段的地址和大小，装载器或程序本身必须为.bss段预留空间。

检查内核ELF文件中所有节的名称、大小和链接地址的完整列表:

```shell
objdump -h obj/kern/kernel
```

```shell
fzh@fzh-virtual-machine:~/mit6.828/lab/obj/kern$ objdump -h kernel
kernel：     文件格式 elf32-i386
节：
Idx Name          Size      VMA       LMA       File off  Algn
  0 .text         00001871  f0100000  00100000  00001000  2**4
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  1 .rodata       00000714  f0101880  00101880  00002880  2**5
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  2 .stab         000038d1  f0101f94  00101f94  00002f94  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  3 .stabstr      000018bb  f0105865  00105865  00006865  2**0
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  4 .data         0000a300  f0108000  00108000  00009000  2**12
                  CONTENTS, ALLOC, LOAD, DATA
  5 .bss          00000648  f0112300  00112300  00013300  2**5
                  CONTENTS, ALLOC, LOAD, DATA
  6 .comment      00000035  00000000  00000000  00013948  2**0
```

​		你将看到比上面列出的更多的部分，但是其他部分对我们的目的来说并不重要。其他的大多数用于保存调试信息，这些信息通常包含在程序的可执行文件中，但不被程序加载器加载到内存中。

特别注意。text部分的“VMA”(或链接地址)和“LMA”(或加载地址)。section的加载地址是该section被加载到内存中的内存地址。

​	section的链接地址就是该section期望执行的起始内存地址。链接器以各种方式在二进制代码中编码链接地址，比如当代码需要全局变量的地址时，如果二进制代码从一个没有链接的地址执行，那么它通常不会工作。

通常，链接地址和加载地址是相同的。例如，看看引导加载程序的.text部分:

```shell
objdump -h obj/boot/boot.out
```

```shell
fzh@fzh-virtual-machine:~/mit6.828/lab$ objdump -h obj/boot/boot.out

obj/boot/boot.out：     文件格式 elf32-i386

节：
Idx Name          Size      VMA       LMA       File off  Algn
  0 .text         00000186  00007c00  00007c00  00000074  2**2
                  CONTENTS, ALLOC, LOAD, CODE
  1 .eh_frame     000000a8  00007d88  00007d88  000001fc  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  2 .stab         00000720  00000000  00000000  000002a4  2**2
                  CONTENTS, READONLY, DEBUGGING
  3 .stabstr      0000088f  00000000  00000000  000009c4  2**0
                  CONTENTS, READONLY, DEBUGGING
  4 .comment      00000035  00000000  00000000  00001253  2**0
                  CONTENTS, READONLY    
```

​		引导加载程序使用ELF程序段表头来决定如何加载这些节。程序表头文件指定要将ELF的哪些部分加载到内存中，以及每个部分应该占用的目标地址。你可以通过以下方式检查程序表头:

```shell
objdump -x obj/kern/kernel
```

​	然后在上述命令下列出程序段表头。ELF中需要加载到内存的区域被标记为“LOAD”的区域。也展示每个程序段表头的其他信息，比如虚拟地址(“vaddr”)、物理地址(“paddr”)和加载区域的大小(“memsz”和“filesz”)。

​	BIOS将引导扇区加载到地址为0x7c00的内存中，因此这是引导扇区的加载地址。这也是引导扇区执行的地方，所以这也是它的链接地址。我们通过将- text 0x7C00传递给boot/Makefrag中的链接器来设置链接地址，因此链接器生成正确的内存地址。

**Exercise 5.**  再次跟踪引导加载程序的前几条指令，并确定如果引导加载程序的链接地址错误，第一个指令将“break”或做错误的事情。通过将boot/Makefrag中的链接地址更改为错误的地址，运行make clean，使用make重新编译lab，并再次跟踪引导加载程序以查看发生了什么。结束后别忘了把链接地址改回来，然后再make clean! 

**答：**这道题希望我们能够去修改boot loader的链接地址，在Lab 1中，作者引入了两个概念，一个是链接地址，一个是加载地址。链接地址可以理解为通过编译器链接器处理形成的可执行程序中指令的地址，即逻辑地址。加载地址则是可执行文件真正被装入内存后运行的地址，即物理地址。

​       在boot loader中，由于在boot loader运行时还没有任何的分段处理机制，或分页处理机制，所以boot loader可执行程序中的链接地址就应该等于加载地址。BIOS默认把boot loader加载到0x7C00内存地址处，所以就要求boot loader的链接地址也要在0x7C00处。boot loader地址的设定是在boot/Makefrag中完成的，所以根据题目的要求，我们需要改动这个文件的值。

首先按照题目要求，在lab目录下输入make clean，清除掉之前编译出来的内核可执行文件，在清除之前你可以先把 obj/boot/boot.asm文件拷贝出来，之后可以用来比较。

boot/Makefrag文件为：

```
$(OBJDIR)/boot/boot: $(BOOT_OBJS)
 27     @echo + ld boot/boot
 28     $(V)$(LD) $(LDFLAGS) -N -e start -Ttext 0x7C00 -o $@.out $^
 29     $(V)$(OBJDUMP) -S $@.out >$@.asm
 30     $(V)$(OBJCOPY) -S -O binary -j .text $@.out $@
 31     $(V)perl boot/sign.pl $(OBJDIR)/boot/boot
```

其中的-Ttext 0x7C00，就是指定链接地址，我们可以把它修改为0x7E00，然后保存退出。

修改之前的obj/boot/boot.asm：

```
 10 .globl start
 11 start:
 12   .code16                     # Assemble for 16-bit mode
 13   cli                         # Disable interrupts
 14     7c00:   fa                      cli
 15   cld                         # String operations increment
 16     7c01:   fc                      cld
 17 
 18   # Set up the important data segment registers (DS, ES, SS).
 19   xorw    %ax,%ax             # Segment number zero
 20     7c02:   31 c0                   xor    %eax,%eax
 21   movw    %ax,%ds             # -> Data Segment
 22     7c04:   8e d8                   mov    %eax,%ds
 23   movw    %ax,%es             # -> Extra Segment
 24     7c06:   8e c0                   mov    %eax,%es
 25   movw    %ax,%ss             # -> Stack Segment
 26     7c08:   8e d0                   mov    %eax,%ss
```

链接地址修改为0x7E00后obj/boot/boot.asm：

```
 10 .globl start
 11 start:
 12   .code16                     # Assemble for 16-bit mode
 13   cli                         # Disable interrupts
 14     7e00:   fa                      cli
 15   cld                         # String operations increment
 16     7e01:   fc                      cld
 17 
 18   # Set up the important data segment registers (DS, ES, SS).
 19   xorw    %ax,%ax             # Segment number zero
 20     7e02:   31 c0                   xor    %eax,%eax
 21   movw    %ax,%ds             # -> Data Segment
 22     7e04:   8e d8                   mov    %eax,%ds
 23   movw    %ax,%es             # -> Extra Segment
 24     7e06:   8e c0                   mov    %eax,%es
 25   movw    %ax,%ss             # -> Stack Segment
 26     7e08:   8e d0                   mov    %eax,%ss
```

可以看出，二者区别在于可执行文件中的链接地址不同了，原来是从0x7C00开始，现在则是从0x7E00开始。

然后我们还是按照原来的方式，调试内核，由于BIOS会把boot loader程序默认装入到0x7c00处，所以我们还是再0x7C00处设置断点，并且运行到那里，结果发现如下：

```shell
(gdb) b *0x7c00
Breakpoint 1 at 0x7c00
(gdb) c
Continuing.
[   0:7c00] => 0x7c00:	cli    

Breakpoint 1, 0x00007c00 in ?? ()
```

可见第一条执行的指令仍旧是正确的，这是因为boot loader实际加载到内存0x7c00处，而程序也是在0x7c00执行，所以我们接着往下一步步运行。接下来的几步仍旧是正常的，但是直到运行到一条指令：

```
[   0:7c1e] => 0x7c1e:	lgdtw  0x7e64
0x00007c1e in ?? ()
(gdb) x/6xb 0x7e64
0x7e64:	0x00	0x00	0x00	0x00	0x00	0x00
```

图中的0x7c1e处指令``` lgdtw 0x7e64```.

​		这条指令我们之前讲述过，是把指令后面的值所指定内存地址处后6个字节的值输入全局描述符表寄存器GDTR，但是当前这条指令读取的内存地址是0x7e64，我们在图中也展示了一下这个地址处后面6个单元存放的值，发现是全部是0。这肯定是不对的，正确的应该是在0x7c64处存放的值，即图中最下面一样的值。可见，问题出在这里，GDTR表的值读取不正确，这是实现从实模式到保护模式转换的非常重要的一步。我们可以继续运行，知道发现下面这句：

```
(gdb) si
[   0:7c2d] => 0x7c2d:	ljmp   $0x8,$0x7e32
0x00007c2d in ?? ()
(gdb)
```

正常情况下，```ljmp   $0x8,$0x7e32```这条指令采用段:偏移量来寻址，$0x8表示段选择子，$0x7e32表示偏移量，但是前面分析描述符表GDT的地址无法找到，这行代码无法正常跳转。

这边解释一下前面的代码可以正常运行，后面就无法运行。链接地址就是逻辑地址(也可以看作后面要学的虚拟地址)，正常程序运行只会根据虚拟地址，再通过映射到物理地址中运行。刚开始为什么可以正常运行是因为bios直接跳转到0x7C00(虚拟地址)中运行，此时由于在boot loader运行时还没有任何的分段处理机制，或分页处理机制，0x7c00(虚拟地址)就是0x7c00(物理地址)，程序可以正常运行，此后一条借着一条指令运行，但是到了```lgdt gdtdesc```指令，将gdtdesc(虚拟地址0x7e64)的后6个单元内容加载到GDTR中，此时gdtdesc的地址由于修改了链接地址从0x7c64变为0x7e64，然后就会把物理地址0x7e64的内容加载到GDTR中，描述符表GDT的地址无法找到，```ljmp   $0x8,$0x7e32```就执行出错。

------

​		回头看看内核的加载地址和链接地址。与引导加载程序不同，这两个地址是不同的:内核告诉引导加载程序将其加载到低地址(1M)内存中，但它期望从高地址执行。我们将在下一节深入研究如何实现这一点。

​		除了section信息之外，ELF头中还有一个字段对我们来说很重要，它叫做e_entry。这个字段保存着程序入口点的链接地址：.text程序开始执行的内存地址。你可以看到 entry point:

```shell
objdump -f obj/kern/kernel
```

```
fzh@fzh-virtual-machine:~/mit6.828/lab$ objdump -f obj/kern/kernel
obj/kern/kernel：     文件格式 elf32-i386
体系结构：i386， 标志 0x00000112：
EXEC_P, HAS_SYMS, D_PAGED
起始地址 0x0010000c
```

现在你应该能够理解boot/main.c中最小的ELF加载程序了。它将内核的每个节从磁盘读取到该节的加载地址内存中，然后跳转到内核的入口点。

**Exercise 6.**  我们可以使用GDB的x命令检查内存，命令x/Nx ADDR打印地址为ADDR起始的N个内存单元内容(注意，命令中的两个“x”都是小写的)。一个字的大小并不是一个通用的标准。在GNU汇编中，一个字是两个字节(xorw中的“w”代表word，意思是两个字节)。重置机器(退出QEMU/GDB并再次启动它们)。在BIOS进入引导加载程序时检查0x00100000处的8个内存字，然后在引导加载程序进入内核时再次检查。为什么它们不同?第二个断点有什么?

答：在BIOS载入boot loader后，查看0x00100000内存单元的值为;

```
(gdb) x/8xb 0x00100000
0x100000:	0x00	0x00	0x00	0x00	0x00	0x00	0x00	0x00
```

从boot loader进入内核后，再一次查看内存单元0x00100000开始的8个字节：

```
(gdb) b *0x0010000c
Breakpoint 1 at 0x10000c
(gdb) c
(gdb) x/8xb 0x00100000
0x100000:	0x02	0xb0	0xad	0x1b	0x00	0x00	0x00	0x00
```

