//========================================================================
// File name  : ksbiglpthw.c
// Description: The hardware source file.
// Author     : Jan Soldan 
// Copyright (C) 2002 - 2004 Jan Soldan			     
// All rights reserved.		
//========================================================================
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/sysctl.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/tty.h>
#include <asm/io.h>

#include "sbigudrv.h"
#include "ksbiglptmain.h"
#include "ksbiglpthw.h"
#include "ksbigdef.h"

extern spinlock_t  d0_spinlock;
//========================================================================
// KAllocatePrivateData
//========================================================================
int KAllocatePrivateData(struct file *filp, 
                         int          port_base, 
			 int          port_span, 
			 int          buffer_size)
{
 char *buff = (char *)NULL;
 struct private_data *pd = (struct private_data *)(filp->private_data);

 // grab spinlock
 spin_lock(&d0_spinlock);

 // check if the device is available, ie. 'pd' should be NULL pointer
 if(pd){
    // device already open
    #ifdef _CHATTY_
    printk(KERN_ERR "%s() : device already open!\n", __FUNCTION__);
    #endif
    // release spinlock
    spin_unlock(&d0_spinlock);
    return(-EBUSY);
 }

 // device is free, we got it...

 // allocate private_data structure
 if((pd = kmalloc(sizeof(struct private_data), GFP_KERNEL)) == NULL){
    printk(KERN_ERR "%s() : kmalloc() : struct private_data : error!\n",
           __FUNCTION__);
    filp->private_data = (struct private_data *)NULL;
    spin_unlock(&d0_spinlock);
    return(-ENOMEM);
 }

 // allocate I/O buffer
 if((buff = kmalloc(buffer_size, GFP_KERNEL)) == NULL){
    printk(KERN_ERR "%s() : kmalloc() : I/O buffer : error!\n", __FUNCTION__);
    kfree(pd);
    spin_unlock(&d0_spinlock);
    return(-ENOMEM);
 }

 // set pointer to private_data inside the filp
 filp->private_data = pd;

 // release spinlock because nobody can change our private data now
 spin_unlock(&d0_spinlock);

 // initialize private_data structure
 pd->port_base          = port_base;
 pd->port_span          = port_span;
 pd->buffer_size        = buffer_size;
 pd->flags              = 0;
 pd->control_out        = 0;
 pd->imaging_clocks_out = 0;
 pd->noBytesRd          = 0;
 pd->noBytesWr          = 0;
 pd->state              = 0;
 pd->buffer             = buff;
 strcpy(pd->dev_name, LDEV_NAME);

 return(0);
}
//========================================================================
// KReleasePrivateData
//========================================================================
int KReleasePrivateData(struct file *filp)
{
 struct private_data *pd  = (struct private_data *)(filp->private_data);

 if(pd){
    // release I/O buffer
    if(pd->buffer){
       kfree(pd->buffer);
    }
    // release private_data
    kfree(pd);
    filp->private_data = NULL;
 }

 return(0);
}
//========================================================================
// KAllocateLptPorts
//========================================================================
int KAllocateLptPorts(struct file *filp)
{
 struct private_data *pd = (struct private_data *)(filp->private_data);

 // request I/O region
 if(request_region(pd->port_base, pd->port_span, pd->dev_name) == NULL){
    printk(KERN_ERR "%s() : port_base %X, port_span %d error!\n",
	   __FUNCTION__, pd->port_base, pd->port_span);
    printk(KERN_CONT "LPT port probably allocated by your printer.\n");
    printk(KERN_CONT "Please uninstall your printer and try again.\n");
    printk(KERN_CONT "Use: modprobe -r lp, modprobe -r parport_pc, modprobe -r parport\n");
    return(-EBUSY);
 }

 return(0);
}
//========================================================================
// KReleaseLptPorts
//========================================================================
int KReleaseLptPorts(struct file *filp)
{
 struct private_data *pd = (struct private_data *)(filp->private_data);
 release_region(pd->port_base, pd->port_span);

 return(0);
}
//========================================================================
// KReallocateLptPorts
//========================================================================
int KReallocateLptPorts(struct file *filp, LinuxLptPortParams *arg)
{
 int                  status = CE_NO_ERROR;
 LinuxLptPortParams   llpp;
 struct private_data *pd = (struct private_data *)(filp->private_data);

 // copy IOC_LINUX_PORT_PARAMS structure from the user space
 status = copy_from_user(&llpp, (LinuxLptPortParams *)arg,
                         sizeof(LinuxLptPortParams)); 					
 if(status != 0){ 					
    printk(KERN_ERR "%s() : copy_from_user : error!\n", __FUNCTION__);
    return(status);
 }

 /*
 printk(KERN_DEBUG "%s() : \n", __FUNCTION__);
 printk(KERN_CONT "current  values : portBase %X, portSpan %d, name %s\n",
        pd->port_base, pd->port_span, pd->dev_name);
 printk(KERN_CONT "requested values: portBase %X, portSpan %d, name %s\n",
	llpp.portBase, llpp.portSpan, pd->dev_name);
 */

 // request I/O region
 if(request_region(llpp.portBase, llpp.portSpan, pd->dev_name) == NULL){
    // somebody holds requested lpt ports...
    printk(KERN_ERR "%s() : request_region() : error!\n", __FUNCTION__);
    printk(KERN_CONT "current  values : portBase %X, portSpan %d, name %s\n",
	   pd->port_base, pd->port_span, pd->dev_name);
    printk(KERN_CONT "requested values: portBase %X, portSpan %d, name %s\n",
	   llpp.portBase, llpp.portSpan, pd->dev_name);
    return(-EBUSY);
 }

 // reallocation OK, release old ports and record changes inside the
 // private data structure 

 // release old LPT ports
 KReleaseLptPorts(filp);

 // set new port's base & span values
 pd->port_base = llpp.portBase;
 pd->port_span = llpp.portSpan;

 return(0);
}
//========================================================================
// KDumpPrivateData
//========================================================================
void KDumpPrivateData(struct file *filp)
{
 struct private_data *pd  = (struct private_data *)(filp->private_data);

 printk(KERN_DEBUG "---------------------------------------------------\n");
 printk(KERN_CONT "Device private data :\n");
 printk(KERN_CONT "---------------------------------------------------\n");
 printk(KERN_CONT "dev_name    : %s\n", pd->dev_name);
 printk(KERN_CONT "port_base   : %x\n", pd->port_base);
 printk(KERN_CONT "port_span   : %x\n", pd->port_span);
 printk(KERN_CONT "buffer      : p = %p,  size = %d\n",
         &(pd->buffer), pd->buffer_size);
 
 if((filp->f_mode & FMODE_READ) && (filp->f_mode & FMODE_WRITE)){
    printk(KERN_CONT  "I/O mode    : device opened for reading and writing\n");
 }else if(filp->f_mode & FMODE_READ){
    printk(KERN_CONT "I/O mode    : device opened for reading\n");
 }else if(filp->f_mode & FMODE_WRITE){
    printk(KERN_CONT "I/O mode    : device opened for writing\n");
 }
 printk(KERN_CONT "---------------------------------------------------\n");
}
//========================================================================
