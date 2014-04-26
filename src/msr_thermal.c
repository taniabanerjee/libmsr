/* File: msr_thermal.c
 *
 * Author: Kathleen Shoga
 * 
 * Referenced: Intel documentation 
 *
*/

#include <stdio.h>
#include <stdint.h>
#include <sys/time.h>
#include <stddef.h>
#include <assert.h>
#include "msr_core.h"
#include "msr_thermal.h"

// Two defines below from Barry Rountree 
#define MASK_RANGE(m,n) ((((uint64_t)1<<((m)-(n)+1))-1)<<(n))
#define MASK_VAL(x,m,n) (((uint64_t)(x)&MASK_RANGE((m),(n)))>>(n))

// Section 35.7 Table 35-11
// Or Section 35.1 Table 35.2
#define IA32_THERM_STATUS		(0x19C) //core scope
#define IA32_THERM_INTERRUPT		(0x19B) //core scope
#define IA32_PACKAGE_THERM_STATUS	(0x1B1) //package scope
#define IA32_PACKAGE_THERM_INTERRUPT	(0x1B2) //package scope
#define MSR_TEMPERATURE_TARGET		(0x1A2)	// unique scope (Noted in documentation but do no know 
						// what it means exactly)

// Static structs being defined
static struct msr_temp_target 		t_target;
static struct therm_stat 		t_stat;
static struct therm_interrupt 		t_interrupt;
static struct pkg_therm_stat 		pkg_stat;
static struct pkg_therm_interrupt 	pkg_interrupt;

void is_init() {
	static int initialized = 0;
	if(!initialized)
	{
		get_temp_target(&t_target);
	}
	else
		return;
}


//---------------------MSR_TEMPERATURE_TARGET functions--------------------------------------------------------

void get_temp_target(struct msr_temp_target *s)
{ 
	read_all_sockets(MSR_TEMPERATURE_TARGET, s->raw);
	//read_msr_by_coord(socket, 0, 0, MSR_TEMPERATURE_TARGET, &(s->raw));
	//s->raw = 64;
	int i;
	for(i=0; i<NUM_SOCKETS; i++)
	{
		s->temp_target[i] = MASK_VAL(s->raw[i], 23, 16);	// Minimum temperature at which PROCHOT will
	}							// be asserted in degree Celsius (Probably 
								// the TCC Activation Temperature)	
}

// There is no set function for this because it is read only


//---------------------------------Thermal Functions (status and interrupts)------------------------------------------------

void get_therm_stat(struct therm_stat *s)
{
	read_all_cores(IA32_THERM_STATUS,s->raw);
	//read_msr_by_coord(socket, core, 0, IA32_THERM_STATUS, &(s->raw));
	//s->raw = 56879; //in dec
	int i;
	for(i = 0; i<NUM_CORES; i++)
	{
		s->status[i] = MASK_VAL(s->raw[i], 0,0);			// Indicates whether the digital thermal sensor
									// high-temperature output signal (PROCHOT#) is
									// currently active.
									// (1=active)
	
		s->status_log[i] = MASK_VAL(s->raw[i], 1,1);			// Indicates the history of the thermal sensor high
									// temperature output signal (PROCHOT#)
									// If equals 1, PROCHOT# has been asserted since a 
									// previous RESET or clear 0 by user
									
		s->PROCHOT_or_FORCEPR_event[i] = MASK_VAL(s->raw[i], 2,2);	// Indicates whether PROCHOT# or FORCEPR# is being 
									// asserted by another agent on the platform
	
		s->PROCHOT_or_FORCEPR_log[i] = MASK_VAL(s->raw[i], 3, 3);	// Indicates whether PROCHOT# or FORCEPR# has been 
									// asserted by aother agent on the platform since
									// the last clearing of the bit or a reset.
									// (1=has been externally asserted)
									// (0 to clear)
									// External PROCHOT# assertions are only acknowledged
									// if the Bidirectional Prochot feature is enabled
		
		s->crit_temp_status[i] = MASK_VAL(s->raw[i], 4, 4);		// Indicates whether actual temp is currently higher
									// than or equal to the value set in Thermal Thresh 1
									// (0 then actual temp is lower)
									// (1 then equal to or higher)
		
		s->crit_temp_log[i] = MASK_VAL(s->raw[i], 5, 5);		// Sticky bit indicates whether the crit temp detector
									// output signal has been asserted since the last reset
									// or clear 
									// (0 cleared) (1 asserted)
	
		s->therm_thresh1_status[i] = MASK_VAL(s->raw[i], 6,6);	// Indicates whether actual temp is currently higher than 
									// or equal to the value set in Thermal Threshold 1
									// (0 actual temp is lower) 
									// (1 actual temp is greater than or equal to TT#1)
		
		s->therm_thresh1_log[i] = MASK_VAL(s->raw[i], 7, 7);		// Sticky bit indicates whether Thermal Threshold #1 has 
									// been reached since last reset or clear 0. 
		
		s->therm_thresh2_status[i] = MASK_VAL(s->raw[i], 8,8);	// Same as therm_thresh1_status, except for Thermal Threshold #2
		s->therm_thresh2_log[i] = MASK_VAL(s->raw[i], 9, 9);		// Same as therm_thresh1_log, except for Thermal Threshold #2
	
		s->power_limit_status[i] = MASK_VAL(s->raw[i], 10, 10);	// Indicates whether the processor is currently operating below
									// OS-requested P-state (specified in IA32_PERF_CTL), or 
									// OS-requested clock modulation duty cycle (in IA32_CLOCK_MODULATION)
									// This field supported only if CPUID.06H:EAX[bit 4] = 1
									// Package level power limit notification can be delivered 
									// independently to IA32_PACKAGE_THERM_STATUS MSR
		
		s->power_notification_log[i] = MASK_VAL(s->raw[i], 11, 11);	// Sticky bit indicates the processor went below OS-requested P-state
									// or OS-requested clock modulation duty cycle since last RESET
									// or clear 0. Supported only if CPUID.06H:EAX[bit 4] = 1
									// Package level power limit notification is indicated independently
									// in IA32_PACKAGE_THERM_STATUS MSR
	
		s->readout[i] = MASK_VAL(s->raw[i], 22, 16);			// Digital temperature reading in 1 degree Celsius relative to the 
									// TCC activation temperature
									// (0: TCC Activation temperature)
									// (1: (TCC Activation -1)... etc. )
	
		s->resolution_deg_celsius[i] = MASK_VAL(s->raw[i], 30, 27);	// Specifies the resolution (tolerance) of the digital thermal
									// sensor. The value is in degrees Celsius. Recommended that
									// new threshold values be offset from the current temperature by 
									// at least the resolution + 1 in order to avoid hysteresis of
									// interrupt generation
	
		s->readout_valid[i] = MASK_VAL(s->raw[i], 31, 31);		// Indicates if the digital readout is valid (valid if = 1)
	}
}

void get_therm_interrupt(struct therm_interrupt *s)
{
	read_all_cores(IA32_THERM_INTERRUPT,s->raw);
	//read_msr_by_coord(socket, core, 0, IA32_THERM_INTERRUPT, &(s->raw));
	//s->raw = 64;	
	int i;
	for(i=0;i<NUM_CORES;i++)
	{
		s->high_temp_enable[i] = MASK_VAL(s->raw[i], 0, 0);	// Allows the BIOS to enable the generation of an inerrupt on the
									// transition from low-temp to a high-temp threshold. 
									// 0 (default) disable[i]s interrupts (1 enables interrupts)
	
		s->low_temp_enable[i] = MASK_VAL(s->raw[i], 1, 1);	// Allows the BIOS to enable generation of an interrupt on the
									// transistion from high temp to low temp (TCC de-activation)
									// 0 (default) disable[i]s interrupts (1 enables interrupts)
	
		s->PROCHOT_enable[i] = MASK_VAL(s->raw[i], 2, 2);	// Allows BIOS or OS to enable generation of an interrupt when 
									// PROCHOT has been asserted by another agent on the platform
									// and the Bidirectional Prochot feature is enable[i]d
									// (0 disable[i]s) (1 enables)
	
		s->FORCEPR_enable[i] = MASK_VAL(s->raw[i], 3, 3);	// Allows the BIOS or OS to enable generation of an interrupt when
									// FORCEPR # has been asserted by another agent on the platform. 
									// (0 disable[i]s the interrupt) (2 enables)
	
		s->crit_temp_enable[i] = MASK_VAL(s->raw[i], 4, 4);	// Enables generations of interrupt when the critical temperature 
									// detector has detected a critical thermal condition. 
									// Recommended responce: system shutdown
									// (0 disable[i]s interrupt) (1 enables)
	
		s->thresh1_val[i] = MASK_VAL(s->raw[i], 14, 8);		// A temp threshold. Encoded relative to the TCC Activation temperature 
									// (same format as digital readout) 
									// used to generate therm_thresh1_status and therm_thresh1_log and
									// Threshold #1 thermal interrupt delivery
	
		s->thresh1_enable[i] = MASK_VAL(s->raw[i], 15, 15);	// Enables generation of an interrupt when the actual temperature  
									// crosses Threshold #1 setting in any direction 
									// (ZERO ENABLES the interrupt) (ONE DISABLES the interrupt)
	
		s->thresh2_val[i] = MASK_VAL(s->raw[i], 22, 16);	// See above description for thresh1_val (just for thresh2)
		s->thresh2_enable[i] = MASK_VAL(s->raw[i], 23, 23);	// See above description for thresh1_enable (just for thresh2)
	
		s->pwr_limit_notification_enable[i] = MASK_VAL(s->raw[i], 24, 24);// Enables generation of power notification events when the processor
										// went below OS-requested P-state or OS-requested clock modulation 
										// duty cycle. 
										// THIS FIELD SUPPORTED ONLY IF CPUID.06H:EAX[bit 4] = 1
										// Package level power limit notification can be enable[i]d independently
										// by IA32_PACKAGE_THERM_INTERRUPT MSR
	}
}

void get_pkg_therm_stat(struct pkg_therm_stat *s)
{
	read_all_sockets(IA32_PACKAGE_THERM_STATUS,s->raw);
	//read_msr_by_coord( package, 0, 0, IA32_PACKAGE_THERM_STATUS, &(s->raw) ); 
	//s->raw = 56879; //in dec
	int i;
	for(i=0;i<NUM_SOCKETS;i++)
	{
		s->status[i] = MASK_VAL(s->raw[i],0,0);			// Indicates whether the digital thermal sensor 
									// high-temp output signal (PROCHOT#) for the pkg
									// currently active. (1=active)
	
		s->status_log[i] = MASK_VAL(s->raw[i],1,1);		// Indicates the history of thermal sensor high
	       								// temp output signal (PROCHOT#) of pkg. 
									// (1= pkg PROCHOT# has been asserted since previous 
									// reset or last time software cleared bit.
	
		s->PROCHOT_event[i] = MASK_VAL(s->raw[i],2,2);		// Indicates whether pkg PROCHOT# is being asserted by
									// another agent on the platform
	
		s->PROCHOT_log[i] = MASK_VAL(s->raw[i],3,3);		// Indicates whether pkg PROCHET# has been asserted by 
									// another agent on the platform since the last clearing
									// of the bit by software or reset. (1= has been externally
									// asserted) (write 0 to clear)
	
		s->crit_temp_status[i] = MASK_VAL(s->raw[i],4,4);	// Indicates whether pkg crit temp detector output signal
									// is currently active (1=active)
	
		s->crit_temp_log[i] = MASK_VAL(s->raw[i],5,5);		// Indicates whether pkg crit temp detector output signal
	       								//been asserted since the last clearing of bit or reset 
									//(1=has been asserted) (set 0 to clear)
	
		s->therm_thresh1_status[i] = MASK_VAL(s->raw[i],6,6);	// Indicates whether actual pkg temp is currently higher 
									// than or equal to value set in Package Thermal Threshold #1
									// (0=actual temp lower) (1= actual temp >= PTT#1)
	
		s->therm_thresh1_log[i] = MASK_VAL(s->raw[i],7,7);	// Indicates whether pkg therm threshold #1 has been reached 
									// since last software clear of bit or reset. (1= reached)
									// (clear with 0)
	
		s->therm_thresh2_status[i] = MASK_VAL(s->raw[i],8,8);	// Same as above (therm_thresh1_stat) except it is for threshold #2
		s->therm_thresh2_log[i] = MASK_VAL(s->raw[i],9,9);	// Same as above (therm_thresh2_log) except it is for treshold #2
	
		s->power_limit_status[i] = MASK_VAL(s->raw[i],10,10);	// Indicates pkg power limit forcing 1 or more processors to  
									// operate below OS-requested P-state
									// (Note: pkg power limit violation may be caused by processor
									// cores or by devices residing in the uncore - examine 
									// IA32_THERM_STATUS to determine if cause from processor core)
	
		s->power_notification_log[i] = MASK_VAL(s->raw[i],11,11);// Indicates any processor from package went below OS-requested
									// P-state or OS-requested clock modulation duty cycle since
									// last clear or RESET
	
		s->readout[i] = MASK_VAL(s->raw[i],22,16); 			// Pkg digital temp reading in 1 degree Celsius relative to
									// the pkg TCC activation temp
									// (0 = Package TTC activation temp)
									// (1 = (PTCC Activation - 1) etc. 
									// Note: lower reading actually higher temp
		}
}

void get_pkg_therm_interrupt(struct pkg_therm_interrupt *s)
{
	read_all_sockets(IA32_PACKAGE_THERM_INTERRUPT, s->raw);
	//read_msr_by_coord( package, 0, 0, IA32_PACKAGE_THERM_INTERRUPT, &(s->raw));
	//s->raw = 56879;
	int i;
	for(i=0;i<NUM_SOCKETS;i++)
	{
		s->high_temp_enable[i] = MASK_VAL(s->raw[i], 0, 0);	// Allows the BIOS to enable the generation of an interrupt on transition
								// from low temp to pkg high temp threshold
								// (0 (default)- disables interrupts) (1=enables interrupts)
	
		s->low_temp_enable[i] = MASK_VAL(s->raw[i], 1, 1);	// Allows BIOS to enable the generation of an interrupt on transition
								// from high temp to a low temp (TCC de-activation)
								// (0 (default)- diabales interrupts) (1=enables interrupts)
	
		s->PROCHOT_enable[i] = MASK_VAL(s->raw[i], 2, 2);	// Allows BIOS or OS to enable generation of an interrupt when pkg PROCHOT#
								// has been asserted by another agent on the platform and the Bidirectional
								// Prochot feature is enabled. (0 disables interrupt) (1 enables interrupt)
	
		s->crit_temp_enable[i] = MASK_VAL(s->raw[i], 4, 4);	// Enables generation of interrupt when pkg crit temp detector has detected
								// a crit thermal condition. Recommended response: system shut down.
								// (0 disables interrupt) (1 enables)
								
		s->thresh1_val[i] = MASK_VAL(s->raw[i], 14, 8);	// A temp threshold, encoded relative to the Package TCC Activation temp
								// using format as Digital Readout
								// Compared against the Package Digital Readout and used to generate 
								// Package Thermal Threshold #1 status and log bits as well as 
								// the Package Threshold #1 thermal interrupt delivery
		s->thresh1_enable[i] = MASK_VAL(s->raw[i], 15, 15);	// Enables the generation of an interrupt when the actual temp crosses 
								// the thresh1_val setting in any direction
								// (0 enables interrupt) (1 disables interrupt)
								
		s->thresh2_val[i] = MASK_VAL(s->raw[i], 22, 16);	// See thresh1_val
		s->thresh2_enable[i] = MASK_VAL(s->raw[i], 23, 23);	// See thresh1_enable
	
		s->pwr_limit_notification_enable[i] = MASK_VAL(s->raw[i], 24, 24);	// Enables generation of package power notification events
	}
}

void set_therm_stat(struct therm_stat *s)
{
	uint64_t msrVal[NUM_CORES];
	read_all_cores(IA32_THERM_STATUS,msrVal);
	//read_msr_by_coord(socket, core, 0, IA32_THERM_STATUS, &msrVal);
	int i;
	for(i=0;i<NUM_CORES;i++)
	{
		assert(s->status_log[i] == 0 || s->status_log[i] == 1);
		assert(s->PROCHOT_or_FORCEPR_log[i] == 0 || s->PROCHOT_or_FORCEPR_log[i] == 1);
		assert(s->crit_temp_log[i] == 0 || s->crit_temp_log[i] == 1);
		assert(s->therm_thresh1_log[i] == 0 || s->therm_thresh1_log[i] == 1);
		assert(s->therm_thresh2_log[i] == 0 || s->therm_thresh2_log[i] == 1);
		assert(s->power_notification_log[i] == 0 || s->power_notification_log[i] == 1);
		
		msrVal[i] = (msrVal[i] & (~(1<<1))) | (s->status_log[i] << 1);
		msrVal[i] = (msrVal[i] & (~(1<<3))) | (s->PROCHOT_or_FORCEPR_log[i] << 1);
		msrVal[i] = (msrVal[i] & (~(1<<5))) | (s->crit_temp_log[i] << 1);
		msrVal[i] = (msrVal[i] & (~(1<<7))) | (s->therm_thresh1_log[i] << 1);
		msrVal[i] = (msrVal[i] & (~(1<<9))) | (s->therm_thresh2_log[i] << 1);
		msrVal[i] = (msrVal[i] & (~(1<<11))) | (s->power_notification_log[i] << 1);
	}
	write_all_cores_v(IA32_THERM_STATUS,msrVal);
	//write_msr_by_coord(socket, core, 0, IA32_THERM_STATUS, msrVal);
//Not sure if I should update the struct here or not.
}

void set_therm_interrupt(struct therm_interrupt *s)
{
	uint64_t msrVal[NUM_CORES];
	read_all_cores(IA32_THERM_INTERRUPT,msrVal);
	//read_msr_by_coord(socket, core, 0, IA32_THERM_INTERRUPT, &msrVal);
	int i;
	for(i=0;i<NUM_CORES;i++)
	{
		assert(s->high_temp_enable[i] == 0 || s->high_temp_enable[i] == 1);
		assert(s->low_temp_enable[i] == 0 || s->low_temp_enable[i] == 1);
		assert(s->PROCHOT_enable[i] == 0 || s->PROCHOT_enable[i] == 1);
		assert(s->FORCEPR_enable[i] == 0 || s->FORCEPR_enable[i] == 1);
		assert(s->crit_temp_enable[i] == 0 || s->crit_temp_enable[i] == 1);
		assert(s->thresh1_enable[i] == 0 || s->thresh1_enable[i] == 1);
		assert(s->thresh2_enable[i] == 0 || s->thresh2_enable[i] == 1);
		assert(s->pwr_limit_notification_enable[i] == 0 || s->pwr_limit_notification_enable[i] == 1);
	
		msrVal[i] = (msrVal[i] & (~(1<<0))) | (s->high_temp_enable[i] << 0);
		msrVal[i] = (msrVal[i] & (~(1<<1))) | (s->low_temp_enable[i] << 1);
		msrVal[i] = (msrVal[i] & (~(1<<2))) | (s->PROCHOT_enable[i] << 2);
		msrVal[i] = (msrVal[i] & (~(1<<3))) | (s->FORCEPR_enable[i] << 3);
		msrVal[i] = (msrVal[i] & (~(1<<4))) | (s->crit_temp_enable[i] << 4);
		msrVal[i] = (msrVal[i] & (~(7<<8))) | (s->thresh1_val[i] << 8);
		msrVal[i] = (msrVal[i] & (~(1<<15))) | (s->thresh1_enable[i] << 15);
		msrVal[i] = (msrVal[i] & (~(7<<16))) | (s->thresh2_val[i] << 16);
		msrVal[i] = (msrVal[i] & (~(1<<23))) | (s->thresh2_enable[i] << 23);
		msrVal[i] = (msrVal[i] & (~(1<<24))) | (s->pwr_limit_notification_enable[i] << 24);

	}
	write_all_cores_v(IA32_THERM_INTERRUPT,msrVal);
	//write_msr_by_coord(socket, core, 0, IA32_THERM_INTERRUPT, msrVal);
}

void set_pkg_therm_stat(struct pkg_therm_stat *s)
{
	uint64_t msrVal[NUM_SOCKETS];
	read_all_sockets(IA32_PACKAGE_THERM_STATUS,msrVal);
	//read_msr_by_coord(package, 0, 0, IA32_PACKAGE_THERM_INTERRUPT, &msrVal);
	int i;
	for(i=0;i<NUM_SOCKETS;i++)
	{
		assert(s->status_log[i] == 0 || s->status_log[i] == 1);
		assert(s->PROCHOT_log[i] == 0 || s->PROCHOT_log[i] == 1);
		assert(s->crit_temp_log[i] == 0 || s->PROCHOT_log[i] == 1);
		assert(s->therm_thresh1_log[i] == 0 || s->therm_thresh1_log[i] == 1);
		assert(s->therm_thresh2_log[i] == 0 || s->therm_thresh2_log[i] == 1);
		assert(s->power_notification_log[i] == 0 || s->power_notification_log[i] == 1);
		
		msrVal[i] = (msrVal[i] & (~(1<<1))) | (s->status_log[i] << 1);
		msrVal[i] = (msrVal[i] & (~(1<<3))) | (s->PROCHOT_log[i] << 3);
		msrVal[i] = (msrVal[i] & (~(1<<5))) | (s->crit_temp_log[i] << 5);
		msrVal[i] = (msrVal[i] & (~(1<<7))) | (s->therm_thresh1_log[i] << 7);
		msrVal[i] = (msrVal[i] & (~(1<<9))) | (s->therm_thresh2_log[i] << 9);
		msrVal[i] = (msrVal[i] & (~(1<<11))) | (s->power_notification_log[i] << 11);
	}
	write_all_sockets_v(IA32_PACKAGE_THERM_STATUS, msrVal);
	//write_msr_by_coord(package, 0, 0, IA32_PACKAGE_THERM_STATUS, msrVal);
}

void set_pkg_therm_interrupt(struct pkg_therm_interrupt *s)
{
	uint64_t msrVal[NUM_SOCKETS];
	read_all_sockets(IA32_PACKAGE_THERM_INTERRUPT,msrVal);
	//read_msr_by_coord(package, 0, 0, IA32_PACKAGE_THERM_INTERRUPT, &msrVal);
	int i;
	for(i=0;i<NUM_SOCKETS;i++)
	{	
		assert(s->high_temp_enable[i] == 0 || s->high_temp_enable[i] == 1);
		assert(s->low_temp_enable[i] == 0 || s->low_temp_enable[i] == 1);
		assert(s->PROCHOT_enable[i] == 0 || s->PROCHOT_enable[i] == 1);
		assert(s->crit_temp_enable[i] == 0 || s->crit_temp_enable[i] == 1);
		assert(s->thresh1_enable[i] == 0 || s->thresh1_enable[i] == 1);
		assert(s->thresh2_enable[i] == 0 || s->thresh2_enable[i] == 1);
		assert(s->pwr_limit_notification_enable[i] == 0 || s->pwr_limit_notification_enable[i] == 1);
	
		msrVal[i] = (msrVal[i] & (~(1<<0))) | (s->high_temp_enable[i] << 0);
		msrVal[i] = (msrVal[i] & (~(1<<1))) | (s->low_temp_enable[i] << 1);
		msrVal[i] = (msrVal[i] & (~(1<<2))) | (s->PROCHOT_enable[i] << 2);
		msrVal[i] = (msrVal[i] & (~(1<<4))) | (s->crit_temp_enable[i] << 4);
		msrVal[i] = (msrVal[i] & (~(7<<8))) | (s->thresh1_val[i] << 8);	
		msrVal[i] = (msrVal[i] & (~(1<<15))) | (s->thresh1_enable[i] << 15);
		msrVal[i] = (msrVal[i] & (~(7<<16))) | (s->thresh2_val[i] << 16);
		msrVal[i] = (msrVal[i] & (~(1<<23))) | (s->thresh2_enable[i] << 23);
		msrVal[i] = (msrVal[i] & (~(1<<24))) | (s->pwr_limit_notification_enable[i] << 24);
	}	
	write_all_sockets_v(IA32_PACKAGE_THERM_INTERRUPT,msrVal);
	//write_msr_by_coord(package, 0, 0, IA32_PACKAGE_THERM_INTERRUPT, msrVal);
}


void dump_thermal_terse_label()
{
	int core,socket;
	for(socket = 0; socket < NUM_SOCKETS; socket++)
	{
		for(core = NUM_CORES_PER_SOCKET*socket; core < NUM_CORES_PER_SOCKET*(socket+1); core++)
		{
			fprintf(stdout,"TempC_%02d_%02d ", socket,core); 
		}
	}
}

void dump_thermal_terse()
{
	is_init();
	get_therm_stat(&t_stat);
	int core, socket;
	int actTemp;

	for(socket=0; socket < NUM_SOCKETS; socket++)
	{
		for(core=NUM_CORES_PER_SOCKET*socket; core < NUM_CORES_PER_SOCKET*(socket+1); core++)
		{
			actTemp = t_target.temp_target[socket] - t_stat.readout[core];
			fprintf(stdout,"%d ", actTemp);
		}
	}
}

void dump_thermal_verbose_label()
{
	for(socket=0; socket < NUM_SOCKETS; socket++)
	{
		// Registers that are socket granularity

		//Thermal Status dump (package)
		fprintf(stdout, "socket_status_%02d ", socket);
		fprintf(stdout, "socket_log_%02d ", socket);
		fprintf(stdout, "socket_PROCHOT_event_%02d ", socket);
		fprintf(stdout, "socket_PROCHOT_log_%02d ", socket);
		fprintf(stdout, "socket_crit_temp_status_%02d ", socket);
		fprintf(stdout, "socket_crit_temp_log_%02d ", socket);
		fprintf(stdout, "socket_therm_thresh1_status_%02d ", socket);
		fprintf(stdout, "socket_therm_thresh1_log_%02d ", socket);
		fprintf(stdout, "socket_therm_thresh2_status_%02d ", socket);
		fprintf(stdout, "socket_therm_thresh2_log_%02d ", socket);
		fprintf(stdout, "socket_power_limit_status_%02d ", socket);
		fprintf(stdout, "socket_power_notification_log_%02d ", socket);
		fprintf(stdout, "socket_readout_%02d ", socket);
		fprintf(stdout, "socket_TempC_%02d ", socket);
		//Thermal Interrupt dump (package)	
		fprintf(stdout, "socket_high_temp_enable_%02d ", socket);
		fprintf(stdout, "socket_low_temp_enable_%02d ", socket);
		fprintf(stdout, "socket_PROCHOT_enable_%02d ", socket);
		fprintf(stdout, "socket_crit_temp_enable_%02d ", socket);
		fprintf(stdout, "socket_thresh1_val_%02d ", socket);
		fprintf(stdout, "socket_thresh1_actual_tempC_%02d ", socket);
		fprintf(stdout, "socket_thresh1_enable_%02d ", socket);
		fprintf(stdout, "socket_thresh2_val_%02d ", socket);
		fprintf(stdout, "socket_thresh2_actual_tempC_%02d ", socket);
		fprintf(stdout, "socket_thresh2_enable_%02d ", socket);
		fprintf(stdout, "socket_pwr_limit_notification_enable_%02d ", socket);
		
		// Registers that are core granularity
		for(core=NUM_CORES_PER_SOCKET*socket; core < NUM_CORES_PER_SOCKET*(socket+1); core++)
		{
			//Thermal Status dump (core)
			fprintf(stdout, "core_status_%02d_%02d ", socket, core);
			fprintf(stdout, "core_log_%02d_%02d ", socket, core);
			fprintf(stdout, "core_PROCHOT_or_FORCEPR_event_%02d_%02d ", socket, core);
			fprintf(stdout, "core_PROCHOT_or_FORCEPR_log_%02d_%02d ", socket, core);
			fprintf(stdout, "core_crit_temp_status_%02d_%02d ", socket, core);
			fprintf(stdout, "core_crit_temp_log_%02d_%02d ", socket, core);
			fprintf(stdout, "core_therm_thresh1_status_%02d_%02d ", socket, core);
			fprintf(stdout, "core_therm_thresh1_log_%02d_%02d ", socket, core);
			fprintf(stdout, "core_therm_thresh2_status_%02d_%02d ", socket, core);
			fprintf(stdout, "core_therm_thresh2_log_%02d_%02d ", socket, core);
			fprintf(stdout, "core_power_limit_status_%02d_%02d ", socket, core);
			fprintf(stdout, "core_power_notification_log_%02d_%02d ", socket, core);
			fprintf(stdout, "core_readout_%02d_%02d ", socket, core);
			fprintf(stdout, "core_TempC_%02d_%02d ", socket, core);
			fprintf(stdout, "core_resolution_deg_celsius_%02d_%02d ", socket, core);
			fprintf(stdout, "core_readout_valid_%02d_%02d ", socket, core);
			//Thermal Interrupt dump (core)	
			fprintf(stdout, "core_high_temp_enable_%02d_%02d ", socket, core);
			fprintf(stdout, "core_low_temp_enable_%02d_%02d ", socket, core);
			fprintf(stdout, "core_PROCHOT_enable_%02d_%02d ", socket, core);
			fprintf(stdout, "core_FORCEPR_enable_%02d_%02d ", socket, core);
			fprintf(stdout, "core_crit_temp_enable_%02d_%02d ", socket, core);
			fprintf(stdout, "core_thresh1_val_%02d_%02d ", socket, core);
			fprintf(stdout, "core_thresh1_actual_tempC_%02d_%02d ", socket, core);
			fprintf(stdout, "core_thresh1_enable_%02d_%02d ", socket, core);
			fprintf(stdout, "core_thresh2_val_%02d_%02d ", socket, core);
			fprintf(stdout, "core_thresh2_actual_tempC_%02d_%02d ", socket, core);
			fprintf(stdout, "core_thresh2_enable_%02d_%02d ", socket, core);
			fprintf(stdout, "core_pwr_limit_notification_enable_%02d_%02d ", socket, core);
			
		}
	}
}

void dump_thermal_verbose()
{
	is_init();
	get_therm_stat(&t_stat);
	get_therm_interrupt(&t_interrupt);
	get_pkg_therm_stat(&pkg_stat);
	get_pkg_therm_interrupt(&pkg_interrupt);
	int core, socket;
	int actTemp;
	//TODO: Change from printing labels to printing values
	for(socket=0; socket < NUM_SOCKETS; socket++)
	{
		// Registers that are socket granularity

		//Thermal Status dump (package)
		fprintf(stdout, "socket_status_%02d ", socket);
		fprintf(stdout, "socket_log_%02d ", socket);
		fprintf(stdout, "socket_PROCHOT_event_%02d ", socket);
		fprintf(stdout, "socket_PROCHOT_log_%02d ", socket);
		fprintf(stdout, "socket_crit_temp_status_%02d ", socket);
		fprintf(stdout, "socket_crit_temp_log_%02d ", socket);
		fprintf(stdout, "socket_therm_thresh1_status_%02d ", socket);
		fprintf(stdout, "socket_therm_thresh1_log_%02d ", socket);
		fprintf(stdout, "socket_therm_thresh2_status_%02d ", socket);
		fprintf(stdout, "socket_therm_thresh2_log_%02d ", socket);
		fprintf(stdout, "socket_power_limit_status_%02d ", socket);
		fprintf(stdout, "socket_power_notification_log_%02d ", socket);
		fprintf(stdout, "socket_readout_%02d ", socket);
		fprintf(stdout, "socket_TempC_%02d ", socket);
		//Thermal Interrupt dump (package)	
		fprintf(stdout, "socket_high_temp_enable_%02d ", socket);
		fprintf(stdout, "socket_low_temp_enable_%02d ", socket);
		fprintf(stdout, "socket_PROCHOT_enable_%02d ", socket);
		fprintf(stdout, "socket_crit_temp_enable_%02d ", socket);
		fprintf(stdout, "socket_thresh1_val_%02d ", socket);
		fprintf(stdout, "socket_thresh1_actual_tempC_%02d ", socket);
		fprintf(stdout, "socket_thresh1_enable_%02d ", socket);
		fprintf(stdout, "socket_thresh2_val_%02d ", socket);
		fprintf(stdout, "socket_thresh2_actual_tempC_%02d ", socket);
		fprintf(stdout, "socket_thresh2_enable_%02d ", socket);
		fprintf(stdout, "socket_pwr_limit_notification_enable_%02d ", socket);
		
		// Registers that are core granularity
		for(core=NUM_CORES_PER_SOCKET*socket; core < NUM_CORES_PER_SOCKET*(socket+1); core++)
		{
			//Thermal Status dump (core)
			fprintf(stdout, "core_status_%02d_%02d ", socket, core);
			fprintf(stdout, "core_log_%02d_%02d ", socket, core);
			fprintf(stdout, "core_PROCHOT_or_FORCEPR_event_%02d_%02d ", socket, core);
			fprintf(stdout, "core_PROCHOT_or_FORCEPR_log_%02d_%02d ", socket, core);
			fprintf(stdout, "core_crit_temp_status_%02d_%02d ", socket, core);
			fprintf(stdout, "core_crit_temp_log_%02d_%02d ", socket, core);
			fprintf(stdout, "core_therm_thresh1_status_%02d_%02d ", socket, core);
			fprintf(stdout, "core_therm_thresh1_log_%02d_%02d ", socket, core);
			fprintf(stdout, "core_therm_thresh2_status_%02d_%02d ", socket, core);
			fprintf(stdout, "core_therm_thresh2_log_%02d_%02d ", socket, core);
			fprintf(stdout, "core_power_limit_status_%02d_%02d ", socket, core);
			fprintf(stdout, "core_power_notification_log_%02d_%02d ", socket, core);
			fprintf(stdout, "core_readout_%02d_%02d ", socket, core);
			fprintf(stdout, "core_TempC_%02d_%02d ", socket, core);
			fprintf(stdout, "core_resolution_deg_celsius_%02d_%02d ", socket, core);
			fprintf(stdout, "core_readout_valid_%02d_%02d ", socket, core);
			//Thermal Interrupt dump (core)	
			fprintf(stdout, "core_high_temp_enable_%02d_%02d ", socket, core);
			fprintf(stdout, "core_low_temp_enable_%02d_%02d ", socket, core);
			fprintf(stdout, "core_PROCHOT_enable_%02d_%02d ", socket, core);
			fprintf(stdout, "core_FORCEPR_enable_%02d_%02d ", socket, core);
			fprintf(stdout, "core_crit_temp_enable_%02d_%02d ", socket, core);
			fprintf(stdout, "core_thresh1_val_%02d_%02d ", socket, core);
			fprintf(stdout, "core_thresh1_actual_tempC_%02d_%02d ", socket, core);
			fprintf(stdout, "core_thresh1_enable_%02d_%02d ", socket, core);
			fprintf(stdout, "core_thresh2_val_%02d_%02d ", socket, core);
			fprintf(stdout, "core_thresh2_actual_tempC_%02d_%02d ", socket, core);
			fprintf(stdout, "core_thresh2_enable_%02d_%02d ", socket, core);
			fprintf(stdout, "core_pwr_limit_notification_enable_%02d_%02d ", socket, core);
			
		}
	}
}

 //---------------------------------Dump Thermal Functions ------------------------------------------------

/*
void dump_core_temp(int socket, int core, struct therm_stat * s)
{
	struct msr_temp_target x;
        get_temp_target(socket, &x);
        int actTemp = x.temp_target - s->readout;
	printf("QQQ %d %d %d", core, socket, actTemp);
}


// old dump functions
void dump_msr_temp_target()
{
	int i;
	is_init();
	fprintf(stdout, "temp_target            = ");
	for(i = 0; i<NUM_SOCKETS;i++)
	{
		fprintf(stdout, "%lu ", t_target.temp_target[i]);	
	}
	fprintf(stdout, "\n");
}
void dump_therm_stat(int socket, int core, struct therm_stat * s)
{
	struct msr_temp_target x;
	get_temp_target(socket, &x);
	int actTemp = x.temp_target - s->readout;
	fprintf(stdout, "status				= %d\n", s->status);
	fprintf(stdout, "status_log			= %d\n", s->status_log);
	fprintf(stdout, "PROCHOT_or_FORCEPR_event	= %d\n", s->PROCHOT_or_FORCEPR_event);
	fprintf(stdout, "PROCHOT_or_FORCEPR_log		= %d\n", s->PROCHOT_or_FORCEPR_log);
	fprintf(stdout, "crit_temp_status		= %d\n", s->crit_temp_status);
	fprintf(stdout, "crit_temp_log			= %d\n", s->crit_temp_log);
	fprintf(stdout, "therm_thresh1_status		= %d\n", s->therm_thresh1_status);
	fprintf(stdout, "therm_thresh1_log		= %d\n", s->therm_thresh1_log);
	fprintf(stdout, "therm_thresh2_status		= %d\n", s->therm_thresh2_status);
	fprintf(stdout, "therm_thresh2_log		= %d\n", s->therm_thresh2_log);
	fprintf(stdout, "power_limit_status		= %d\n", s->power_limit_status);
	fprintf(stdout, "power_notification_log		= %d\n", s->power_notification_log);
	fprintf(stdout, "readout				= %d\n", s->readout);
	fprintf(stdout, "Actual temperature:		= %d degrees Celsius\n", actTemp);
	fprintf(stdout, "resolution_deg_celsius		= %d\n", s->resolution_deg_celsius);
	fprintf(stdout, "readout_valid			= %d\n", s->readout_valid);
	fprintf(stdout, "\n");
}

void dump_therm_interrupt(int socket, int core, struct therm_interrupt *s)
{
	struct msr_temp_target x;
	get_temp_target(socket, &x);
	int actTemp1 = x.temp_target - s->thresh1_val;
	int actTemp2 = x.temp_target - s->thresh2_val;
	fprintf(stdout, "high_temp_enable		= %d\n", s->high_temp_enable);
	fprintf(stdout, "low_temp_enable			= %d\n", s->low_temp_enable);
	fprintf(stdout, "PROCHOT_enable			= %d\n", s->PROCHOT_enable);
	fprintf(stdout, "FORCEPR_enable			= %d\n", s->FORCEPR_enable);
	fprintf(stdout, "crit_temp_enable		= %d\n", s->crit_temp_enable);
	fprintf(stdout, "thresh1_val			= %d\n", s->thresh1_val);
	fprintf(stdout, "Thresh1 actual temperature	= %d degrees Celsius\n", actTemp1);
	fprintf(stdout, "thresh1_enable			= %d\n", s->thresh1_enable);
	fprintf(stdout, "thresh2_val			= %d\n", s->thresh2_val);
	fprintf(stdout, "Thresh1 actual temperature	= %d degrees Celsius\n", actTemp2);
	fprintf(stdout, "thresh2_enable			= %d\n", s->thresh2_enable);
	fprintf(stdout, "pwr_limit_notification_enable	= %d\n", s->pwr_limit_notification_enable);
	fprintf(stdout, "\n");
}

void dump_pkg_therm_stat(int package, struct pkg_therm_stat * s)
{
	struct msr_temp_target x;
	get_temp_target(package, &x);		// use core 0 of the specified package 
						// hopefully the PTCC temp is the same regardless
						// of which core we choose
	int actTemp = x.temp_target - s->readout;
	fprintf(stdout, "status			= %d\n", s->status);
        fprintf(stdout, "status_log		= %d\n", s->status_log);
        fprintf(stdout, "PROCHOT_event		= %d\n", s->PROCHOT_event);
	fprintf(stdout, "PROCHOT_log		= %d\n", s->PROCHOT_log);
        fprintf(stdout, "crit_temp_status	= %d\n", s->crit_temp_status);
        fprintf(stdout, "crit_temp_log		= %d\n", s->crit_temp_log);
	fprintf(stdout, "therm_thresh1_status	= %d\n", s->therm_thresh1_status);
        fprintf(stdout, "therm_thresh1_log	= %d\n", s->therm_thresh1_log);
        fprintf(stdout, "therm_thresh2_status	= %d\n", s->therm_thresh2_status);
	fprintf(stdout, "therm_thresh2_log	= %d\n", s->therm_thresh2_log);
	fprintf(stdout, "power_limit_status	= %d\n", s->power_limit_status);
	fprintf(stdout, "power_notification_log	= %d\n", s->power_notification_log);	
	fprintf(stdout, "readout			= %d\n", s->readout);
	fprintf(stdout, "Actual Temperature	= %d degrees Celsius\n", actTemp);
	fprintf(stdout, "\n");
}

void dump_pkg_therm_interrupt(int package, struct pkg_therm_interrupt *s)
{
	struct msr_temp_target x;
	get_temp_target(package, &x);		// see comment in similar case above
	int actTemp1 = x.temp_target - s->thresh1_val;
	int actTemp2 = x.temp_target - s->thresh2_val;
	fprintf(stdout, "high_temp_enable		= %4d\n", s->high_temp_enable);
	fprintf(stdout, "low_temp_enable			= %4d\n", s->low_temp_enable);
	fprintf(stdout, "PROCHOT_enable			= %4d\n", s->PROCHOT_enable);
	fprintf(stdout, "crit_temp_enable		= %4d\n", s->crit_temp_enable);
	fprintf(stdout, "thresh1_val			= %4d\n", s->thresh1_val);
	fprintf(stdout, "Threshold1 actual temperature	= %4d degrees Celsius\n", actTemp1);
	fprintf(stdout, "thresh1_enable			= %4d\n", s->thresh1_enable);
	fprintf(stdout, "thresh2_val			= %4d\n", s->thresh2_val);
	fprintf(stdout, "Threshold2 actual temperature	= %4d degrees Celsius\n", actTemp2);
	fprintf(stdout, "thresh2_enable			= %4d\n", s->thresh2_enable);
	fprintf(stdout, "pwr_limit_notification_enable	= %4d\n", s->pwr_limit_notification_enable);
	fprintf(stdout, "\n");
}
*/
