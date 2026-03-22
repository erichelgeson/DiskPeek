/*
 * HFS Browser Library - Fix-A-Fork type/creator detection
 * Based on BlueSCSI SCSITransfer faf_file_ext.c by Eric Helgeson
 */

#include "faf.h"
#include <cstring>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

namespace hfsbrowse {

struct FAFExtEntry { const char* ext; const char type[5]; const char creator[5]; };

static const FAFExtEntry s_faf_ext_table[] = {
    {"1st","TEXT","ttxt"},{"669","6669","SNPL"},{"8med","STrk","SCPL"},
    {"8svx","8SVX","SCPL"},{"aif","AIFF","SCPL"},{"aifc","AIFC","SCPL"},
    {"aiff","AIFF","SCPL"},{"al","ALAW","SCPL"},{"arc","mArc","SITx"},
    {"arj","BINA","DArj"},{"asc","TEXT","ttxt"},{"asm","TEXT","ttxt"},
    {"au","ULAW","TVOD"},{"avi","VfW ","TVOD"},{"bas","TEXT","ttxt"},
    {"bat","TEXT","ttxt"},{"bin","BINA","SITx"},{"bmp","BMPp","ogle"},
    {"bz","Bzp2","SITx"},{"c","TEXT","KAHL"},{"class","Clss","CWIE"},
    {"cmd","TEXT","ttxt"},{"com","PCFA","SWIN"},{"cpp","TEXT","CWIE"},
    {"cpt","PACT","SITx"},{"csv","TEXT","XCEL"},{"cur","CUR ","GKON"},
    {"cvs","drw2","DAD2"},{"cwj","CWSS","cwkj"},{"doc","WDBN","MSWD"},
    {"dot","sDBN","MSWD"},{"dsk","dimg","dCpy"},{"dvi","ODVI","xdvi"},
    {"dxf","TEXT","SWVL"},{"eps","EPSF","vgrd"},{"epsf","EPSF","vgrd"},
    {"exe","PCFA","SWIN"},{"faq","TEXT","ttxt"},{"fla","SPA ","MFL2"},
    {"flc","FLI ","TVOD"},{"fli","FLI ","TVOD"},{"fm","FMPR","FMPR"},
    {"gif","GIFf","ogle"},{"gz","SIT!","SITx"},{"h","TEXT","KAHL"},
    {"hqx","TEXT","SITx"},{"htm","TEXT","MOSS"},{"html","TEXT","MOSS"},
    {"ico","ICO ","GKON"},{"iff","ILBM","GKON"},{"img","dImg","ddsk"},
    {"ini","TEXT","ttxt"},{"iso","rodh","ddsk"},{"java","TEXT","CWIE"},
    {"jfif","JPEG","ogle"},{"jpeg","JPEG","ogle"},{"jpg","JPEG","ogle"},
    {"lha","LHA ","SITx"},{"lzh","LHA ","SITx"},{"mac","PICT","ogle"},
    {"mcw","WDBN","MSWD"},{"me","TEXT","ttxt"},{"mid","Midi","TVOD"},
    {"midi","Midi","TVOD"},{"mod","STrk","SCPL"},{"moov","MooV","TVOD"},
    {"mov","MooV","TVOD"},{"mp2","MPEG","TVOD"},{"mp3","MPG3","TVOD"},
    {"mpa","MPEG","TVOD"},{"mpeg","MPEG","TVOD"},{"mpg","MPEG","TVOD"},
    {"nfo","TEXT","ttxt"},{"p","TEXT","CWIE"},{"pas","TEXT","CWIE"},
    {"pbm","PPGM","GKON"},{"pct","PICT","ogle"},{"pcx","PCXx","GKON"},
    {"pdf","PDF ","CARO"},{"pgm","PPGM","GKON"},{"pic","PICT","ogle"},
    {"pict","PICT","ogle"},{"pit","PIT ","SITx"},{"pl","TEXT","McPL"},
    {"png","PNG ","ogle"},{"pntg","PNTG","ogle"},{"ppm","PPGM","GKON"},
    {"ps","TEXT","vgrd"},{"psd","8BPS","8BIM"},{"qt","MooV","TVOD"},
    {"qxd","XDOC","XPR3"},{"raw","rodh","ddsk"},{"readme","TEXT","ttxt"},
    {"rgb","SGI ","GKON"},{"rme","TEXT","ttxt"},{"rsrc","rsrc","RSED"},
    {"rtf","TEXT","MSWD"},{"s3m","S3M ","SNPL"},{"sea","APPL","????"},
    {"sgi",".SGI","ogle"},{"sit","SIT!","SITx"},{"snd","BINA","SCPL"},
    {"swf","SWFL","SWF2"},{"tar","TARF","SITx"},{"tex","TEXT","OTEX"},
    {"text","TEXT","ttxt"},{"tga","TPIC","GKON"},{"tgz","Gzip","SITx"},
    {"tif","TIFF","ogle"},{"tiff","TIFF","ogle"},{"toast","CDr3","GImg"},
    {"txt","TEXT","ttxt"},{"url","AURL","Arch"},{"uu","TEXT","SITx"},
    {"uue","TEXT","SITx"},{"voc","VOC ","SCPL"},{"wav","WAVE","TVOD"},
    {"wmf","WMF ","GKON"},{"wp","WP5 ","WPC2"},{"wri","WDBN","MSWD"},
    {"xbm","XBM ","GKON"},{"xlc","XLC ","XCEL"},{"xls","XLS ","XCEL"},
    {"xlw","XLW ","XCEL"},{"xm","XM  ","SNPL"},{"xpm","XPM ","GKON"},
    {"zip","ZIP ","SITx"},{"zoo","Zoo ","Booz"},
};

bool detect_type_creator_ext(const char* filename, TypeCreatorResult* out) {
    const char* dot = strrchr(filename, '.');
    if (!dot || dot[1] == '\0') return false;
    const char* ext = dot + 1;
    for (size_t i = 0; i < sizeof(s_faf_ext_table)/sizeof(s_faf_ext_table[0]); i++) {
        if (strcasecmp(ext, s_faf_ext_table[i].ext) == 0) {
            memcpy(out->type, s_faf_ext_table[i].type, 5);
            memcpy(out->creator, s_faf_ext_table[i].creator, 5);
            return true;
        }
    }
    return false;
}

bool detect_type_creator_magic(const uint8_t* data, size_t len, TypeCreatorResult* out) {
    if (len < 4) return false;
    if (len >= 45 && memcmp(data + 34, "BinHex 4.0", 10) == 0) {
        memcpy(out->type, "TEXT", 5); memcpy(out->creator, "SITx", 5); return true;
    }
    if (len >= 16 && memcmp(data, "StuffIt (c)1997", 15) == 0) {
        memcpy(out->type, "SITD", 5); memcpy(out->creator, "SIT!", 5); return true;
    }
    if (memcmp(data, "SIT!", 4) == 0) {
        memcpy(out->type, "SIT!", 5); memcpy(out->creator, "SIT!", 5); return true;
    }
    if (data[0] == 'P' && data[1] == 'K') {
        memcpy(out->type, "ZIP ", 5); memcpy(out->creator, "SITx", 5); return true;
    }
    if (memcmp(data, "GIF8", 4) == 0) {
        memcpy(out->type, "GIFf", 5); memcpy(out->creator, "ogle", 5); return true;
    }
    if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        memcpy(out->type, "PNG ", 5); memcpy(out->creator, "ogle", 5); return true;
    }
    if (data[0] == 0xFF && data[1] == 0xD8) {
        memcpy(out->type, "JPEG", 5); memcpy(out->creator, "ogle", 5); return true;
    }
    if (len >= 5 && memcmp(data, "%PDF-", 5) == 0) {
        memcpy(out->type, "PDF ", 5); memcpy(out->creator, "CARO", 5); return true;
    }
    if (len >= 54 && data[52] == 0x01 && data[53] == 0x00) {
        memcpy(out->type, "dImg", 5); memcpy(out->creator, "dCpy", 5); return true;
    }
    return false;
}

} // namespace hfsbrowse
