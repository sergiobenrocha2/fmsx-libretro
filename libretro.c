#include "libretro.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "MSX.h"
#include "EMULib.h"
#include "Sound.h"

static uint16_t* image_buffer;
static unsigned image_buffer_width;
static unsigned image_buffer_height;

static uint16_t XPal[80];
static uint16_t BPal[256];
static uint16_t XPal0;


#define SND_RATE 48000

#define WIDTH  272
#define HEIGHT 228
#ifdef PSP
#define PIXEL(R,G,B)    (pixel)(((31*(B)/255)<<11)|((63*(G)/255)<<5)|(31*(R)/255))
#else
#define PIXEL(R,G,B)    (pixel)(((31*(R)/255)<<11)|((63*(G)/255)<<5)|(31*(B)/255))
#endif

#define XBuf image_buffer
#define WBuf image_buffer
#include "CommonMux.h"

uint8_t XKeyState[20];
#define XKBD_SET(K) XKeyState[Keys[K][0]]&=~Keys[K][1]
#define XKBD_RES(K) XKeyState[Keys[K][0]]|=Keys[K][1]

retro_log_printf_t log_cb = NULL;
static retro_video_refresh_t video_cb = NULL;
static retro_input_poll_t input_poll_cb = NULL;
static retro_input_state_t input_state_cb = NULL;
static retro_environment_t environ_cb = NULL;
static retro_audio_sample_batch_t audio_batch_cb = NULL;
static struct retro_perf_callback perf_cb = {};

static retro_perf_tick_t max_frame_ticks = 0;


typedef struct
{
   int retro;
   int fmsx;
}keymap_t;
keymap_t keymap[] =
{
{ RETROK_LEFT,      KBD_LEFT     },
{ RETROK_UP,        KBD_UP       },
{ RETROK_RIGHT,     KBD_RIGHT    },
{ RETROK_DOWN,      KBD_DOWN     },
{ RETROK_LSHIFT,    KBD_SHIFT    },
{ RETROK_RSHIFT,    KBD_SHIFT    },
{ RETROK_LCTRL,     KBD_CONTROL  },
{ RETROK_RCTRL,     KBD_CONTROL  },
{ RETROK_LALT,      KBD_GRAPH    },
{ RETROK_BACKSPACE, KBD_BS       },
{ RETROK_TAB,       KBD_TAB      },
{ RETROK_CAPSLOCK,  KBD_CAPSLOCK },
{ RETROK_END,       KBD_SELECT   },
{ RETROK_HOME,      KBD_HOME     },
{ RETROK_RETURN,    KBD_ENTER    },
{ RETROK_DELETE,    KBD_DELETE   },
{ RETROK_INSERT,    KBD_INSERT   },
{ RETROK_PAGEUP,    KBD_COUNTRY  },
{ RETROK_PAUSE,     KBD_STOP     },
{ RETROK_F1,        KBD_F1       },
{ RETROK_F2,        KBD_F2       },
{ RETROK_F3,        KBD_F3       },
{ RETROK_F4,        KBD_F4       },
{ RETROK_F5,        KBD_F5       },
{ RETROK_KP0,       KBD_NUMPAD0  },
{ RETROK_KP1,       KBD_NUMPAD1  },
{ RETROK_KP2,       KBD_NUMPAD2  },
{ RETROK_KP3,       KBD_NUMPAD3  },
{ RETROK_ESCAPE,    KBD_ESCAPE   },
{ RETROK_KP4,       KBD_NUMPAD4  },
{ RETROK_KP5,       KBD_NUMPAD5  },
{ RETROK_KP6,       KBD_NUMPAD6  },
{ RETROK_KP7,       KBD_NUMPAD7  },
{ RETROK_SPACE,     KBD_SPACE    },
{ RETROK_KP8,       KBD_NUMPAD8  },
{ RETROK_KP9,       KBD_NUMPAD9  }
};

int joystate;
#define JOY_SET(K) joystate |= K
// TODO: Use a less hacky method than hard-coding an offset into the joymap.
const int joy_keyboard_begin = 6;

keymap_t joymap[] = 
{
{ RETRO_DEVICE_ID_JOYPAD_UP,        JST_UP},
{ RETRO_DEVICE_ID_JOYPAD_DOWN,   JST_DOWN },
{ RETRO_DEVICE_ID_JOYPAD_LEFT,   JST_LEFT },
{ RETRO_DEVICE_ID_JOYPAD_RIGHT, JST_RIGHT },
{ RETRO_DEVICE_ID_JOYPAD_A,     JST_FIREA },
{ RETRO_DEVICE_ID_JOYPAD_B,     JST_FIREB },
{ RETRO_DEVICE_ID_JOYPAD_X,        KBD_F3 },
{ RETRO_DEVICE_ID_JOYPAD_Y,     KBD_SPACE },
{ RETRO_DEVICE_ID_JOYPAD_START,    KBD_F1 },
{ RETRO_DEVICE_ID_JOYPAD_SELECT,   KBD_F2 },
{ RETRO_DEVICE_ID_JOYPAD_L,        KBD_F4 },
{ RETRO_DEVICE_ID_JOYPAD_R,        KBD_F5 },
{ RETRO_DEVICE_ID_JOYPAD_L2,    KBD_GRAPH },
{ RETRO_DEVICE_ID_JOYPAD_R2,  KBD_CONTROL },
{ RETRO_DEVICE_ID_JOYPAD_L3,    KBD_ENTER },
{ RETRO_DEVICE_ID_JOYPAD_R3,   KBD_ESCAPE },
};

void retro_get_system_info(struct retro_system_info *info)
{
   info->library_name = "fMSX";
   info->library_version = "3.9";
   info->need_fullpath = true;
   info->block_extract = false;
   info->valid_extensions = "rom|mx1|mx2";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   info->geometry.base_width = image_buffer_width ;
   info->geometry.base_height = image_buffer_height ;
   info->geometry.max_width = 640 ;
   info->geometry.max_height = 480 ;
   info->geometry.aspect_ratio = 0;
   info->timing.fps = 60.0;
   info->timing.sample_rate = SND_RATE;
}

void retro_init(void)
{
   int i;
   struct retro_log_callback log;


   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   else
      log_cb = NULL;

   image_buffer = malloc(640*480*sizeof(uint16_t));
   image_buffer_width =  272;
   image_buffer_height =  228;

   environ_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf_cb);

}

void retro_deinit(void)
{
   if (image_buffer)
      free(image_buffer);

   image_buffer = NULL;
   image_buffer_width = 0;
   image_buffer_height = 0;

   perf_cb.perf_log();

   log_cb(RETRO_LOG_INFO, "maximum frame ticks : %llu\n", max_frame_ticks);
}

static void set_input_descriptors(void)
{
	struct retro_input_descriptor descriptors[] = {
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "D-Pad Left" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,       "D-Pad Up" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "D-Pad Down" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,               "B" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,               "A" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,              "F3" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,        "Spacebar" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,         "F2" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,          "F1" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,              "F4" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,              "F5" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,          "Graph" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,           "Ctrl" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,         "Return" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,         "Escape" },
		{ 0 },
	};

	environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, descriptors);
}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;
   static const struct retro_controller_description port[] = {
      { "RetroKeyboard", RETRO_DEVICE_KEYBOARD },
//      { "RetroPad", RETRO_DEVICE_JOYPAD }
   };

   static const struct retro_controller_info ports[] = {
      { port, 1 },
//      { port, 1 },
      { 0 },
   };
   static const struct retro_variable vars[] = {
      { "fmsx_mode", "MSX Mode; MSX2+|MSX1|MSX2" },
      { "fmsx_video_mode", "MSX Video Mode; NTSC|PAL" },
      { "fmsx_mapper_type_mode", "MSX Mapper Type Mode; Guess Mapper Type A|Guess Mapper Type B" },
      { NULL, NULL },
   };

   cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
}

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t unused) { }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }

void retro_set_controller_port_device(unsigned port, unsigned device)
{
}

void retro_reset(void)
{
   ResetMSX(Mode,RAMPages,VRAMPages);
}

size_t retro_serialize_size(void)
{
   return 0x100000;
}

bool retro_serialize(void *data, size_t size)
{
   if (!SaveState(data, size))
      return false;

   return true;
}

bool retro_unserialize(const void *data, size_t size)
{
   if (LoadState((unsigned char*)data, size) == 0)
      return false;

   return true;
}

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned a, bool b, const char * c) {}

void PutImage(void)
{
   ExitNow = 1;

}

static void check_variables(void)
{
   bool reset_sfx = false;
   struct retro_variable var;
   var.key = "fmsx_mode";
   var.value = NULL;

   Mode = 0;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "MSX1") == 0)
         Mode |= MSX_MSX1;
      else if (strcmp(var.value, "MSX2") == 0)
         Mode |= MSX_MSX2;
      else if (strcmp(var.value, "MSX2+") == 0)
         Mode |= MSX_MSX2P;
   }
   else
   {
      Mode |= MSX_MSX2P;
   }

   var.key = "fmsx_video_mode";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "NTSC") == 0)
         Mode |= MSX_NTSC;
      else if (strcmp(var.value, "PAL") == 0)
         Mode |= MSX_PAL;
   }
   else
   {
      Mode |= MSX_NTSC;
   }

   var.key = "fmsx_mapper_type_mode";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "Guess Mapper Type A") == 0)
         Mode |= MSX_GUESSA;
      else if (strcmp(var.value, "Guess Mapper Type B") == 0)
         Mode |= MSX_GUESSB;
   }
   else
   {
      Mode |= MSX_GUESSA;
   }
}

bool retro_load_game(const struct retro_game_info *info)
{
   int i;
   static char ROMName_buffer[MAXCARTS][1024];
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;

   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      if (log_cb)
         log_cb(RETRO_LOG_INFO, "RGB565 is not supported.\n");
      return false;
   }

   environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &ProgDir);

   check_variables();
   set_input_descriptors();

   Verbose=1;

   UPeriod=100;

   strcpy(ROMName_buffer[0], info->path);
   ROMName[0]=ROMName_buffer[0];
   SETJOYTYPE(0,1);
//   ProgDir=".";

   static Image fMSX_image;
   fMSX_image.Cropped = 0;
   fMSX_image.D = 16;
   fMSX_image.Data = image_buffer;
   fMSX_image.W = image_buffer_width;
   fMSX_image.H = image_buffer_height;
   fMSX_image.L = image_buffer_width;

   GenericSetVideo(&fMSX_image,0,0,image_buffer_width,image_buffer_height);

   for(i = 0; i < 80; i++)
      SetColor(i, 0, 0, 0);

   for(i = 0; i < 256; i++)
     BPal[i]=PIXEL(((i>>2)&0x07)*255/7,((i>>5)&0x07)*255/7,(i&0x03)*255/3);

   memset((void *)XKeyState,0xFF,sizeof(XKeyState));

   InitSound(SND_RATE, 0);
   SetChannels(255/MAXCHANNELS, (1<<MAXCHANNELS)-1);

   ExitNow = 1;
   StartMSX(Mode,RAMPages,VRAMPages);
   printf ("Mode %i, RAMPages %i, VRAMPages %i", Mode, RAMPages, VRAMPages);
   return true;
}

void SetColor(byte N,byte R,byte G,byte B)
{
  if(N)
     XPal[N]=PIXEL(R,G,B);
  else
     XPal0=PIXEL(R,G,B);
}

int PauseAudio(int Switch)
{
   return 1;
}

unsigned int GetFreeAudio(void)
{
  return 1024;
}

unsigned int WriteAudio(sample *Data,unsigned int Length)
{
   static uint16_t audio_buf[1024 * 2];
   int i;
   if (Length > 1024)
      Length = 1024;
   for (i=0; i < Length; i++)
   {
      audio_buf[i << 1]=Data[i];
      audio_buf[(i << 1) + 1]=Data[i];
   }


   return audio_batch_cb(audio_buf, Length);
}

unsigned int Joystick(void)
{
   return joystate;
}

void Keyboard(void)
{
}

unsigned int Mouse(byte N)
{
}

unsigned int GetJoystick(void)
{
   return 1;
}
bool retro_load_game_special(unsigned a, const struct retro_game_info *b, size_t c)
{
   return false;
}

void retro_unload_game(void)
{
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

void *retro_get_memory_data(unsigned id)
{
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   return 0;
}

#define RETRO_PERFORMANCE_INIT(name) \
   retro_perf_tick_t current_ticks;\
   static struct retro_perf_counter name = {#name};\
   if (!name.registered) perf_cb.perf_register(&(name));\
   current_ticks = name.total

#define RETRO_PERFORMANCE_START(name) perf_cb.perf_start(&(name))
#define RETRO_PERFORMANCE_STOP(name) \
   perf_cb.perf_stop(&(name));\
   current_ticks = name.total - current_ticks;\
   if (max_frame_ticks < current_ticks) max_frame_ticks = current_ticks


#ifdef PSP
#include <pspgu.h>
#endif
void retro_run(void)
{
   int i;
   bool updated = false;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      check_variables();

   input_poll_cb();

   for (i=0; i < 130; i++)
      KBD_RES(i);

   for (i=0; i < sizeof(keymap)/sizeof(keymap_t); i++)
      if (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, keymap[i].retro))
         KBD_SET(keymap[i].fmsx);

   joystate = 0;

   for (i = 0; i < sizeof(joymap) / sizeof(keymap_t); i++)
   {
	   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, joymap[i].retro))
	   {
		   if (i < joy_keyboard_begin)
			   JOY_SET(joymap[i].fmsx);
		   else
			   KBD_SET(joymap[i].fmsx);
	   }
   }

   RETRO_PERFORMANCE_INIT(core_retro_run);
   RETRO_PERFORMANCE_START(core_retro_run);

   RunZ80(&CPU);
   RenderAndPlayAudio(SND_RATE / 60);

   RETRO_PERFORMANCE_STOP(core_retro_run);

   fflush(stdout);

#ifdef PSP
   static unsigned int __attribute__((aligned(16))) d_list[32];
   void* const texture_vram_p = (void*) (0x44200000 - (640 * 480)); // max VRAM address - frame size

   sceKernelDcacheWritebackRange(XBuf, 256*240 );
   sceGuStart(GU_DIRECT, d_list);
   sceGuCopyImage(GU_PSM_5650, 0, 0, image_buffer_width, image_buffer_height, image_buffer_width, image_buffer, 0, 0, image_buffer_width, texture_vram_p);

   sceGuTexSync();
   sceGuTexImage(0, 512, 256, image_buffer_width, texture_vram_p);
   sceGuTexMode(GU_PSM_5650, 0, 0, GU_FALSE);
   sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
   sceGuDisable(GU_BLEND);
   sceGuFinish();

   video_cb(texture_vram_p, image_buffer_width, image_buffer_height, image_buffer_width * sizeof(uint16_t));
#else
   video_cb(image_buffer, image_buffer_width, image_buffer_height, image_buffer_width * sizeof(uint16_t));
#endif



}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }
