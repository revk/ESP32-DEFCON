/* DEFCON app */
/* Copyright ©2019 - 2024 Adrian Kennard, Andrews & Arnold Ltd.See LICENCE file for details .GPL 3.0 */

static const char __attribute__((unused)) TAG[] = "DEFCON";

#include "revk.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_crt_bundle.h"
#include <hal/spi_types.h>
#include <driver/gpio.h>

httpd_handle_t webserver = NULL;
led_strip_handle_t strip = NULL;
int8_t defcon_level = 9;

#define	LIGHTS	(sizeof(lights)/sizeof(*lights))

static void
web_head (httpd_req_t * req, const char *title)
{
   revk_web_head (req, title);
   httpd_resp_sendstr_chunk (req, "<style>"     //
                             "a.defcon{text-decoration:none;border:1px solid black;border-radius:50%;margin:2px;padding:3px;display:inline-block;width:1em;text-align:center;}" //
                             "a.on{border:3px solid black;}"    //
                             "a.d1{background-color:white;}"    //
                             "a.d2{background-color:red;}"      //
                             "a.d3{background-color:yellow;}"   //
                             "a.d4{background-color:green;color:white;}"        //
                             "a.d5{background-color:blue;color:white;}" //
                             "body{font-family:sans-serif;background:#8cf;}"    //
                             "</style><body><h1>");
   if (title)
      httpd_resp_sendstr_chunk (req, title);
   httpd_resp_sendstr_chunk (req, "</h1>");
}

static esp_err_t
web_root (httpd_req_t * req)
{
   if (revk_link_down ())
      return revk_web_settings (req);   // Direct to web set up
   web_head (req, *hostname ? hostname : appname);
   {                            // Defcon controls
      size_t len = httpd_req_get_url_query_len (req);
      char q[2] = { };
      if (len == 1)
      {
         httpd_req_get_url_query_str (req, q, sizeof (q));
         if (isdigit ((int) *q))
            defcon_level = *q - '0';
         else if (*q == '+' && defcon_level < 9)
            defcon_level++;
         else if (*q == '-' && defcon_level > 0)
            defcon_level--;
      }
      for (int i = 0; i <= 9; i++)
         if (i <= 6 || i == 9)
         {
            q[0] = '0' + i;
            httpd_resp_sendstr_chunk (req, "<a href='?");
            httpd_resp_sendstr_chunk (req, q);
            httpd_resp_sendstr_chunk (req, "' class='defcon d");
            httpd_resp_sendstr_chunk (req, q);
            if (i == defcon_level)
               httpd_resp_sendstr_chunk (req, " on");
            httpd_resp_sendstr_chunk (req, "'>");
            httpd_resp_sendstr_chunk (req, i == 9 ? "X" : q);
            httpd_resp_sendstr_chunk (req, "</a>");
         }
   }
   return revk_web_foot (req, 0, 1, NULL);
}

void
set_io (revk_gpio_t g, int led, int set)
{                               // Set the GPIO and LED STRIP to a value
   revk_gpio_set (g, set);
   if (strip && led < leds)
      revk_led (strip, led, 0xFF, revk_rgb (set ? 'G' : 'R'));
}

void
defcon_task (void *arg)
{
   int8_t level = -1;           // Current DEFCON level
   while (1)
   {
      usleep (10000);
      if (level != defcon_level)
      {                         // Change
         usleep (100000);
         if (level != defcon_level)
         {                      // Double check?
            int8_t waslevel = level;
            level = defcon_level;
            // Report
            jo_t j = jo_object_alloc ();
            jo_int (j, "level", level);
            revk_info ("defcon", &j);
            // Off existing
            for (int i = 0; i < LIGHTS; i++)
               set_io (lights[i], 1 + i, 0);
            if (level < defconblink)
               set_io (clicker, 1 + LIGHTS, 1);
            if (!level)
            {                   // On all
               for (int i = 0; i < LIGHTS; i++)
               {                // All on
                  usleep (200000);
                  set_io (lights[i], 1 + i, 1);
                  if (level < defconblink)
                     set_io (clicker, 1 + LIGHTS, i & 1);
               }
            } else
            {                   // On new level
               usleep (200000);
               if (level <= LIGHTS)
                  set_io (lights[level - 1], level, 1);
               set_io (clicker, 1 + LIGHTS, 0);
            }
            // Beep count
            int beeps = 0;
            if (level < defconbeep)
               beeps = waslevel < level ? 1 : level ? 2 : 3;
            while (beeps--)
            {
               usleep (250000);
               set_io (beeper, 2 + LIGHTS, 1);
               usleep (250000);
               set_io (beeper, 2 + LIGHTS, 0);
            }
            sleep (1);
         }
      }
   }
}

void
led_task (void *arg)
{
   while (1)
   {
      revk_led (strip, 0, 255, revk_blinker ());
      led_strip_refresh (strip);
      usleep (100000);
   }
}

void
blinker_task (void *arg)
{
   while (1)
   {
      set_io (blinker, 3 + LIGHTS, 0);
      revk_gpio_set (blinker, 0);
      sleep(1);
      if (defcon_level < defconblink)
         set_io (blinker, 3 + LIGHTS, 1);
      sleep(1);
   }
}

char *
setdefcon (int level, char *value)
{                               // DEFCON state
   // With value it is used to turn on/off a defcon state, the lowest set dictates the defcon level
   // With no value, this sets the DEFCON state directly instead of using lowest of state set
   static uint8_t state = 0;    // DEFCON state
   if (*value)
   {
      if (*value == '1' || *value == 't' || *value == 'y')
         state |= (1 << level);
      else
         state &= ~(1 << level);
      int l;
      for (l = 0; l < 8 && !(state & (1 << l)); l++);
      defcon_level = l;
   } else
      defcon_level = level;
   return "";
}

const char *
app_callback (int client, const char *prefix, const char *target, const char *suffix, jo_t j)
{
   char value[1000];
   int len = 0;
   *value = 0;
   if (j)
   {
      len = jo_strncpy (j, value, sizeof (value));
      if (len < 0)
         return "Expecting JSON string";
      if (len > sizeof (value))
         return "Too long";
   }
   if (prefix && !strcmp (prefix, topic) && target && isdigit ((int) *target) && !target[1])
      return setdefcon (*target - '0', value);
   if (client || !prefix || target || strcmp (prefix, prefixcommand) || !suffix)
      return NULL;              //Not for us or not a command from main MQTT
   if (isdigit ((int) *suffix) && !suffix[1])
      return setdefcon (*suffix - '0', value);
   if (!strcmp (suffix, "connect"))
   {
      char *t = NULL;
      asprintf (&t, "%s/#", topic);
      lwmqtt_subscribe (revk_mqtt (0), t);
      free (t);
   }
   return NULL;
}

// --------------------------------------------------------------------------------
// Web
#ifdef	CONFIG_REVK_APCONFIG
#error 	Clash with CONFIG_REVK_APCONFIG set
#endif

void
app_main ()
{
   revk_boot (&app_callback);
   revk_start ();

   for (int i = 0; i < LIGHTS; i++)
      revk_gpio_output (lights[i]);
   revk_gpio_output (blinker);
   revk_gpio_output (beeper);
   revk_gpio_output (clicker);

   if (rgb.set && leds)
   {
      led_strip_config_t strip_config = {
         .strip_gpio_num = rgb.num,
         .max_leds = leds,      // LIGHTS, blinker, beeper, clicker, and status
         .led_pixel_format = LED_PIXEL_FORMAT_GRB,      // Pixel format of your LED strip
         .led_model = LED_MODEL_WS2812, // LED strip model
         .flags.invert_out = blink[0].invert,
      };
      led_strip_rmt_config_t rmt_config = {
         .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
         .resolution_hz = 10 * 1000 * 1000,     // 10MHz
#ifdef  CONFIG_IDF_TARGET_ESP32S3
         .flags.with_dma = true,
#endif
      };
      REVK_ERR_CHECK (led_strip_new_rmt_device (&strip_config, &rmt_config, &strip));
   }
   // Web interface
   httpd_config_t config = HTTPD_DEFAULT_CONFIG ();
   if (!httpd_start (&webserver, &config))
   {
      {
         httpd_uri_t uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = web_root,
            .user_ctx = NULL
         };
         REVK_ERR_CHECK (httpd_register_uri_handler (webserver, &uri));
      }
      revk_web_settings_add (webserver);
   }

   revk_task ("defcon", defcon_task, NULL, 4);
   if (blinker.set)
      revk_task ("blinker", blinker_task, NULL, 4);
   if (strip)
      revk_task ("led", led_task, NULL, 4);

   while (1)
      sleep (1);
}
