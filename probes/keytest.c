#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#define VK_F1 0x70
#define VK_F24 0x87
#define VK_XBUTTON1 5
#define VK_XBUTTON2 6
#define VK_MBUTTON 4
#define KM_SHIFT 1
#define KM_CTRL 2
#define KM_ALT 4
typedef struct Key { int vk, mods; char label[24]; } Key;
/* "Shift+Q", "Ctrl+Alt+F", "F5", "Mouse4", "5", or a Windows virtual-key code ("#81") */
static int ParseKey(const char* text, Key* out)
{
    Key k = {0, 0, ""};
    char buf[64];
    snprintf(buf, sizeof buf, "%s", text);
    for (char* tok = strtok(buf, "+ \t\r\n"); tok; tok = strtok(NULL, "+ \t\r\n"))
    {
        char t[24];
        size_t n = 0;
        for (; tok[n] && n < sizeof t - 1; n++) t[n] = (char)toupper((unsigned char)tok[n]);
        t[n] = 0;
        if (!strcmp(t, "SHIFT")) k.mods |= KM_SHIFT;
        else if (!strcmp(t, "CTRL") || !strcmp(t, "CONTROL")) k.mods |= KM_CTRL;
        else if (!strcmp(t, "ALT")) k.mods |= KM_ALT;
        else if (k.vk) return 0;  /* two main keys */
        else if (n == 1 && ((t[0] >= 'A' && t[0] <= 'Z') || (t[0] >= '0' && t[0] <= '9'))) k.vk = t[0];
        else if (t[0] == 'F' && n <= 3 && atoi(t + 1) >= 1 && atoi(t + 1) <= 24) k.vk = VK_F1 + atoi(t + 1) - 1;
        else if (!strcmp(t, "MOUSE4")) k.vk = VK_XBUTTON1;
        else if (!strcmp(t, "MOUSE5")) k.vk = VK_XBUTTON2;
        else if (!strcmp(t, "MOUSE3") || !strcmp(t, "MIDDLEMOUSE")) k.vk = VK_MBUTTON;
        else if (t[0] == '#' && atoi(t + 1) > 0 && atoi(t + 1) < 255) k.vk = atoi(t + 1);
        else return 0;
    }
    if (!k.vk) return 0;
    char name[24];
    if ((k.vk >= 'A' && k.vk <= 'Z') || (k.vk >= '0' && k.vk <= '9')) snprintf(name, sizeof name, "%c", k.vk);
    else if (k.vk >= VK_F1 && k.vk <= VK_F24) snprintf(name, sizeof name, "F%d", k.vk - VK_F1 + 1);
    else if (k.vk == VK_XBUTTON1) snprintf(name, sizeof name, "M4");
    else if (k.vk == VK_XBUTTON2) snprintf(name, sizeof name, "M5");
    else if (k.vk == VK_MBUTTON) snprintf(name, sizeof name, "M3");
    else snprintf(name, sizeof name, "#%d", k.vk);
    snprintf(k.label, sizeof k.label, "%s%s%s%s", k.mods & KM_CTRL ? "Ctrl+" : "", k.mods & KM_ALT ? "Alt+" : "",
        k.mods & KM_SHIFT ? "Shift+" : "", name);
    *out = k;
    return 1;
}


int main(){const char* c[]={" Shift+Q\n","Z"," ctrl + alt + f "," F5","Mouse4","Shift+Q+W","#81","shift","9"};
for(int i=0;i<9;i++){Key k;int ok=ParseKey(c[i],&k);printf("%-20s -> %d vk=%d mods=%d %s\n",c[i],ok,ok?k.vk:0,ok?k.mods:0,ok?k.label:"");}}