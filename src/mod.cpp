// Dusklight Custom Music - version 1.0 (experimentale, melange aleatoire par sauvegarde, musiques de combat)
//
// Remplace des musiques de fond du jeu (sequences) par tes propres fichiers .ast.
//
// Deux facons de l'utiliser, dans le dossier "music" du mod :
//   - un fichier dont le nom est celui d'un morceau du jeu (ex: RANCH.ast) remplace
//     exactement ce morceau ;
//   - tous les autres fichiers .ast (n'importe quel nom) forment une "reserve" qui
//     est repartie au hasard sur les musiques de zone du jeu (champs, villages,
//     donjons...). Chaque SAUVEGARDE a sa propre graine, enregistree avec elle
//     (SaveService) : une nouvelle partie = un nouveau melange, et charger une
//     partie existante redonne le meme melange. Pour imposer la meme graine a
//     toutes les sauvegardes, cree un fichier seed_force.txt (un nombre ou un mot).
//
// Principe technique :
//   1. au demarrage, on declare les .ast au jeu comme de nouveaux streams
//      (AudioResService) ;
//   2. quand le jeu demande une musique remplacee (Z2SeqMgr::bgmStart), on annule la
//      sequence d'origine et on lance a la place le stream avec le systeme de musique
//      streamee du jeu (bgmStreamPrepare / Play) ;
//   3. quand le jeu arrete la musique (Z2SeqMgr::bgmStop), on arrete le stream.

// Le jeu redefinit "nullptr" (via global.h, inclus automatiquement), ce qui casse les
// en-tetes standard de MSVC comme <filesystem>/<chrono>. On les inclut donc en premier,
// avec cette redefinition mise de cote temporairement.
#pragma push_macro("nullptr")
#undef nullptr
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#pragma pop_macro("nullptr")

#include "mods/service.hpp"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.hpp"
#include "mods/svc/host.h"
#include "mods/svc/overlay.h"
#include "mods/svc/save.h"
#include "mods/svc/audio_res.h"
#include "mods/svc/ui.h"

// Game includes
#include "Z2AudioLib/Z2SeqMgr.h"

#include <fmt/format.h>

// Mettre 0 si la compilation echoue a cause du panneau d'interface.
#define ENABLE_UI_PANEL 1

DEFINE_MOD();

IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(HostService, svc_host);
IMPORT_SERVICE(OverlayService, svc_overlay);
IMPORT_SERVICE(SaveService, svc_save);
IMPORT_SERVICE(AudioResService, svc_audio_res);
#if ENABLE_UI_PANEL
IMPORT_SERVICE(UiService, svc_ui);
#endif

DEFINE_HOOK(&Z2SeqMgr::bgmStart, SeqBgmStart);
DEFINE_HOOK(&Z2SeqMgr::bgmStop, SeqBgmStop);
DEFINE_HOOK(&Z2SeqMgr::checkBgmIDPlaying, SeqCheckBgmIDPlaying);
DEFINE_HOOK(&Z2SeqMgr::bgmStreamStop, SeqBgmStreamStop);
DEFINE_HOOK(&Z2SeqMgr::startBattleBgm, SeqStartBattleBgm);

// ---------------------------------------------------------------------------
// Noms des musiques du jeu (enum Z2BgmID de Z2SeqMgr.h)
// ---------------------------------------------------------------------------

#define BGM_LIST(X) \
    X(FIELD_LINK_DAY) X(FIELD_LINK_NIGHT) X(FIELD) X(DUNGEON) X(HORSE_BATTLE) X(TOAL_VILLEGE) \
    X(COWBOY_GAME) X(FACE_OFF_BATTLE) X(BOOMERAMG_MONKEY) X(DUNGEON_FOREST) X(ITEM_GET) \
    X(ITEM_GET_MINI) X(BOSSBABA_0) X(BOSSBABA_1) X(BOSSBABA_2) X(BATTLE_NORMAL) X(RANCH) \
    X(WILD_GOAT) X(OPEN_BOX) X(GAME_OVER) X(ITEM_GET_ME) X(HORSE_BATTLE_D02) X(TWILIGHT) \
    X(EVENT01) X(DUNGEON_LV2) X(TOAL_NIGHT) X(DEATH_MOUNTAIN01) X(BATTLE_TWILIGHT) X(HEART_GET) \
    X(MAGNE_GORON) X(SHOP01) X(SHOP02) X(DUNGEON_LV3) X(MAGNE_GORON_D01) X(MAGNE_GORON_D02) \
    X(FILONE_FOREST) X(SUMO) X(BOSSFIREMAN_0) X(KAKARIKO) X(DEKUTOAD) X(DEKUTOAD_D01) X(RODEO) \
    X(BOSSFIREMAN_1) X(SUMO_D1) X(INDOOR) X(DUNGEON_LV4) X(STATUE_GAME) X(EVENT02) \
    X(BOSS_OCTAEEL_0) X(BOSS_OCTAEEL_1) X(BOSS_OCTAEEL_D01) X(BOSS_OCTAEEL_D02) X(DESERT) \
    X(ZORA_VILLAGE) X(BRIDGE_ON_FIRE) X(DUNGEON_LV5) X(DEMO10) X(VARIANT) X(CARGO_GAME_TW) \
    X(CASTLE_TOWN) X(DUNGEON_LV6) X(DUNGEON_LV7) X(DUNGEON_LV8) X(DUNGEON_LV9_02) \
    X(DEATH_MOUNTAIN02) X(HOLY_FOREST) X(FISHING) X(FISHING_BARE) X(FISHING_GET1) \
    X(FISHING_GET2) X(FISHING_GET3) X(LUTERA1) X(LUTERA2) X(FISHING_HIT) X(SNOW_MOUNTAIN) \
    X(SNOW_BOARD) X(BOSS_SNOWWOMAN_0) X(BOSS_SNOWWOMAN_1) X(DEMO08) X(DEMO13_SEQ) X(CHURCH1) \
    X(CHURCH2) X(HOWL_TOBIKUSA) X(HOWL_UMAKUSA) X(HOWL_ZELDASONG) X(HOWL_LIGHT_PRLD) \
    X(HIDDEN_VIL_D1) X(HIDDEN_VILLAGE) X(SHOP_CELEB) X(SHOP_MARO) X(LAKE) X(STONE_ZELDASONG) \
    X(STONE_LIGHT_PRLD) X(BOSS_SNOWWOMAN_D1) X(ROOM_2) X(MIDNA_SOS) X(FORTUNE) X(IB_MBOSS) \
    X(BOSS_ZANT) X(IB_MBOSS_D01) X(SUMOMO) X(RAKKA_HOUSE) X(LIGHT_PRLD_DUO) X(SOUL_REQ_STONE) \
    X(SOUL_REQ_HOWL) X(SOUL_REQ_DUO) X(LUTERA_DEMO) X(TEMPLE_OF_TIME) X(TN_MBOSS) X(OBACHAN) \
    X(RIVER_GAME) X(GG_MBOSS) X(OUGI_TRAINING) X(HEALING_STONE) X(HEALING_HOWL) X(HEALING_DUO) \
    X(NEW_01_STONE) X(NEW_01_HOWL) X(NEW_01_DUO) X(SUB_DUNGEON) X(WCS_GAME) X(WCS_D01) \
    X(MINIGAME_ROOM) X(NEW_02_STONE) X(NEW_02_HOWL) X(NEW_02_DUO) X(NEW_03_STONE) X(NEW_03_HOWL) \
    X(NEW_03_DUO) X(ITEM_GET_INSECT) X(ITEM_GET_SMELL) X(ITEM_GET_POU) X(GG_MBOSS_D01) X(P_ZANT) \
    X(VS_GANON_01) X(VS_GANON_02) X(VS_GANON_04) X(HARAGIGANT_D01) X(HARAGIGANT_D02) \
    X(HARAGIGANT_BTL01) X(HARAGIGANT_BTL02) X(DRAGON_D01) X(DRAGON_D02) X(DRAGON_BTL01) \
    X(DRAGON_BTL02) X(KOROKORO_GAME) X(YAMIMUSHI_B_D01) X(GOMA_D01) X(GOMA_BTL01) X(GOMA_BTL02) \
    X(FACE_OFF_BATTLE2) X(FACE_OFF_BATTLE3) X(SNOWBOARD_WIN) X(SNOWBOARD_LOSE) X(INDOOR_03) \
    X(INDOOR_04) X(MINIGAME_WIN01) X(MINIGAME_WIN02) X(POSTMAN) X(EVENT05) X(ITEM_GET_ME_S) \
    X(D30_01_NEXT_REQ) X(D30_02_NEXT_REQ) X(TARO_RESCUE) X(KOMONJO_GET_INTRO) X(RIVER_GAME_00) \
    X(ZORA_D01) X(SUMO_D02) X(TN_MBOSS_LV9) X(MENU_SELECT) X(TEMP) X(TEMP_O)

struct BgmName {
    const char* name;
    uint32_t id;
};

static const BgmName kBgmNames[] = {
#define X(n) {#n, static_cast<uint32_t>(Z2BGM_##n)},
    BGM_LIST(X)
#undef X
};

// Musiques de "zone" qui peuvent recevoir un morceau tire au hasard.
// (Les jingles, fanfares, menus, hurlements, combats et boss ne sont pas touches.)
#define ZONE_LIST(X) \
    X(FIELD_LINK_DAY) X(FIELD_LINK_NIGHT) X(FIELD) X(DUNGEON) X(TOAL_VILLEGE) X(TOAL_NIGHT) \
    X(DUNGEON_FOREST) X(RANCH) X(TWILIGHT) X(DUNGEON_LV2) X(DUNGEON_LV3) X(DUNGEON_LV4) \
    X(DUNGEON_LV5) X(DUNGEON_LV6) X(DUNGEON_LV7) X(DUNGEON_LV8) X(DUNGEON_LV9_02) \
    X(DEATH_MOUNTAIN01) X(DEATH_MOUNTAIN02) X(KAKARIKO) X(DESERT) X(ZORA_VILLAGE) \
    X(CASTLE_TOWN) X(HOLY_FOREST) X(SNOW_MOUNTAIN) X(LAKE) X(HIDDEN_VILLAGE) X(FILONE_FOREST) \
    X(INDOOR) X(INDOOR_03) X(INDOOR_04) X(SHOP01) X(SHOP02) X(CHURCH1) X(CHURCH2) X(ROOM_2) \
    X(SUB_DUNGEON) X(TEMPLE_OF_TIME) X(MINIGAME_ROOM)

static const uint32_t kZoneIds[] = {
#define X(n) static_cast<uint32_t>(Z2BGM_##n),
    ZONE_LIST(X)
#undef X
};

static std::string to_lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

static const char* bgm_name(uint32_t id) {
    for (const BgmName& b : kBgmNames) {
        if (b.id == id) {
            return b.name;
        }
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Chemins (UTF-8 -> std::filesystem, pour les noms d'utilisateur avec accents)
// ---------------------------------------------------------------------------

static std::filesystem::path path_from_utf8(const char* s) {
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s)));
#else
    return std::filesystem::path(s);
#endif
}

static std::string path_to_utf8(const std::filesystem::path& p) {
#if defined(__cpp_char8_t)
    std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
#else
    return p.u8string();
#endif
}

// ---------------------------------------------------------------------------
// Etat du mod
// ---------------------------------------------------------------------------

struct Track {
    std::string name;       // nom du fichier sans extension
    std::filesystem::path file;
    std::string discPath;   // chemin virtuel donne au jeu
    uint16_t streamId = 0;  // ID attribue par AudioResService
    AudioSoundTableHandle streamHandle = 0;
    bool loaded = false;    // le fichier a deja ete envoye au jeu (overlay)
    bool failed = false;    // trop d'echecs : on laisse la musique d'origine
    int failCount = 0;
    double durationSec = 0; // duree du morceau, lue dans l'en-tete du .ast
    OverlayHandle overlay = 0;  // enregistrement de l'overlay du fichier complet
};

enum class State { Idle, Loading, Preparing, Playing };

static std::deque<Track> g_files;                      // tous les fichiers .ast trouves
static std::unordered_map<uint32_t, Track*> g_tracks;  // ID de musique du jeu -> fichier
static uint32_t g_curBgm = 0;                          // musique du jeu actuellement remplacee

// Melange par sauvegarde
static std::vector<Track*> g_pool;                      // fichiers a repartir au hasard
static std::unordered_map<uint32_t, Track*> g_shuffled; // zone -> fichier, pour la sauvegarde actuelle
static std::unordered_set<uint32_t> g_zoneSet;          // zones eligibles au melange
static bool g_shuffleValid = false;
static uint64_t g_shuffleSeed = 0;
static bool g_forcedSeed = false;
static uint64_t g_forcedSeedValue = 0;
static State g_state = State::Idle;
static Track* g_cur = nullptr;
static int g_wait = 0;
static int g_quickDeaths = 0;  // le stream s'est arrete moins de 3 s apres son lancement, plusieurs fois de suite
static std::chrono::steady_clock::time_point g_startTime;  // debut de la lecture en cours

// Reprise de la musique la ou elle s'est arretee : quand le jeu coupe le stream (changement
// de salle...), on prepare un fichier "reprise" qui commence a la bonne position. Deux
// emplacements en alternance, pour ne jamais toucher a un fichier que le jeu pourrait
// encore tenir ouvert.
static const char* kResumePath[2] = {"/Audiores/Stream/custom_resume_a.ast",
                                     "/Audiores/Stream/custom_resume_b.ast"};
static uint16_t g_resumeStreamId[2] = {0, 0};
static AudioSoundTableHandle g_resumeStreamHandle[2] = {0, 0};
static OverlayHandle g_resumeOverlay[2] = {0, 0};
static int g_resumeSlot = 0;          // emplacement du fichier de reprise en cours
static bool g_resumeAvailable = false;
static bool g_resumeDisabled = false; // la reprise a echoue pour cette musique
static bool g_playingResume = false;  // le stream en cours est un fichier de reprise (sans boucle)
static double g_posBase = 0;          // position (s) dans le morceau au debut de la lecture en cours
static double g_tailSec = 0;          // duree du fichier de reprise en cours
static std::deque<Track*> g_loadedOrder;  // morceaux charges en memoire, du plus ancien au plus recent

// Musiques de combat (dossier music/combat)
static std::vector<Track*> g_combat;
static std::mt19937_64 g_rng{std::random_device{}()};
static bool g_inBattle = false;
static Track* g_fieldTrack = nullptr;  // musique de zone interrompue par le combat
static double g_fieldPos = 0;          // position (s) ou la reprendre a la fin du combat
static Track* g_lastCombat = nullptr;
static std::chrono::steady_clock::time_point g_battleStart;
static std::chrono::steady_clock::time_point g_calmSince;
static bool g_calm = false;
static bool g_internal = false;  // vrai quand c'est NOUS qui appelons bgmStop
// Z2GetSeqMgr() ne fonctionne pas depuis un mod (chaque DLL a sa propre copie de la
// variable globale, qui reste vide). On garde donc le pointeur que le jeu nous donne
// lui-meme dans les hooks.
static Z2SeqMgr* g_seq = nullptr;
static std::string g_musicDirText;
static std::vector<std::string> g_panelLines;
static std::ofstream g_logFile;
static std::string g_logPathText;

// Journal : ecrit dans le journal de Dusklight ET dans custom_music_log.txt
// (a cote du dossier "music"), pour pouvoir me l'envoyer facilement.
static void emit_log(int level, const std::string& text) {
    if (level == 2) {
        mods::log::error("{}", text);
    } else if (level == 1) {
        mods::log::warn("{}", text);
    } else {
        mods::log::info("{}", text);
    }
    if (g_logFile.is_open()) {
        g_logFile << (level == 2 ? "ERREUR: " : level == 1 ? "ATTENTION: " : "") << text << "\n";
        g_logFile.flush();
    }
}

template <typename... Args>
static void cm_log(fmt::format_string<Args...> f, Args&&... args) {
    emit_log(0, fmt::format(f, std::forward<Args>(args)...));
}
template <typename... Args>
static void cm_log_warn(fmt::format_string<Args...> f, Args&&... args) {
    emit_log(1, fmt::format(f, std::forward<Args>(args)...));
}
template <typename... Args>
static void cm_log_err(fmt::format_string<Args...> f, Args&&... args) {
    emit_log(2, fmt::format(f, std::forward<Args>(args)...));
}

static uint32_t stream_sound_id(const Track& t) {
    // JAISoundID : section 2 (musique streamee), groupe 0, puis l'ID du stream.
    return (2u << 24) | t.streamId;
}

// ID du stream qu'on s'attend a voir actif : le fichier de reprise ou le morceau complet.
static uint32_t current_sound_id(const Track& t) {
    if (g_playingResume) {
        return (2u << 24) | g_resumeStreamId[g_resumeSlot];
    }
    return stream_sound_id(t);
}

static bool read_all(const std::filesystem::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) {
        return false;
    }
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    return size == 0 || static_cast<bool>(f.read(reinterpret_cast<char*>(out.data()), size));
}

static uint32_t rd32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

static void wr32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

// Construit un .ast qui commence au bloc contenant la position posSec (sans boucle).
static bool make_resume_ast(const std::vector<uint8_t>& ast, double posSec, std::vector<uint8_t>& out,
                            double& startSec, double& tailSec) {
    if (ast.size() < 0x40 || std::memcmp(ast.data(), "STRM", 4) != 0) {
        return false;
    }
    uint16_t format = static_cast<uint16_t>((ast[8] << 8) | ast[9]);
    uint16_t channels = static_cast<uint16_t>((ast[0x0C] << 8) | ast[0x0D]);
    uint32_t rate = rd32(&ast[0x10]);
    uint32_t total = rd32(&ast[0x14]);
    uint32_t blockBytes = rd32(&ast[0x20]);
    if (format != 1 || channels != 2 || rate == 0 || blockBytes < 2) {
        return false;
    }
    const uint32_t spb = blockBytes / 2;  // echantillons par bloc et par canal
    const size_t stride = 0x20 + 2 * static_cast<size_t>(blockBytes);
    const uint32_t numBlocks = (total + spb - 1) / spb;
    uint32_t k = static_cast<uint32_t>(posSec * rate / spb);
    if (k == 0 || k >= numBlocks) {
        return false;
    }
    size_t off = 0x40 + static_cast<size_t>(k) * stride;
    if (off + 0x20 >= ast.size()) {
        return false;
    }
    uint32_t newTotal = total - k * spb;
    out.assign(ast.begin(), ast.begin() + 0x40);
    out.insert(out.end(), ast.begin() + static_cast<std::ptrdiff_t>(off), ast.end());
    wr32(&out[0x04], static_cast<uint32_t>(out.size() - 0x40));
    out[0x0E] = 0;  // pas de boucle : quand la reprise se termine, on relance le morceau complet
    out[0x0F] = 0;
    wr32(&out[0x14], newTotal);
    wr32(&out[0x18], 0);
    wr32(&out[0x1C], newTotal);
    startSec = static_cast<double>(k) * spb / rate;
    tailSec = static_cast<double>(newTotal) / rate;
    return true;
}

// Prepare le fichier de reprise a la position posSec. Retourne faux si impossible.
static bool try_resume(Track& t, double posSec) {
    if (!g_resumeAvailable || t.durationSec <= 0 || posSec < 3.0 || posSec > t.durationSec - 3.0) {
        return false;
    }
    std::vector<uint8_t> src;
    if (!read_all(t.file, src)) {
        return false;
    }
    std::vector<uint8_t> out;
    double startSec = 0;
    double tailSec = 0;
    if (!make_resume_ast(src, posSec, out, startSec, tailSec)) {
        return false;
    }
    int slot = g_resumeSlot ^ 1;
    if (g_resumeOverlay[slot] != 0) {
        svc_overlay->remove(mod_ctx, g_resumeOverlay[slot]);
        g_resumeOverlay[slot] = 0;
    }
    OverlayHandle h = 0;
    if (svc_overlay->add_buffer(mod_ctx, kResumePath[slot], out.data(), out.size(), &h) != MOD_OK) {
        return false;
    }
    g_resumeOverlay[slot] = h;
    g_resumeSlot = slot;
    g_playingResume = true;
    g_posBase = startSec;
    g_tailSec = tailSec;
    cm_log("reprise de {} a {:.1f} s ({:.1f} Mo)", t.name, startSec, out.size() / 1048576.0);
    return true;
}

// Envoie le fichier .ast au jeu (en memoire). Fait une seule fois par morceau.
static bool ensure_loaded(Track& t) {
    if (t.loaded) {
        g_wait = 0;
        return true;
    }
    std::vector<uint8_t> buf;
    if (!read_all(t.file, buf)) {
        cm_log_err("impossible de lire {}", path_to_utf8(t.file));
        return false;
    }
    OverlayHandle h = 0;
    ModResult r = svc_overlay->add_buffer(mod_ctx, t.discPath.c_str(), buf.data(), buf.size(), &h);
    if (r != MOD_OK) {
        cm_log_err("overlay add_buffer a echoue pour {}", t.name);
        return false;
    }
    t.overlay = h;
    t.loaded = true;
    g_wait = 3;  // l'overlay est applique au prochain "frame boundary" : on attend un peu

    // On garde au plus 3 morceaux en memoire : on decharge les plus anciens (sauf celui en cours).
    g_loadedOrder.push_back(&t);
    while (g_loadedOrder.size() > 3) {
        Track* victim = nullptr;
        for (auto it = g_loadedOrder.begin(); it != g_loadedOrder.end(); ++it) {
            if (*it != g_cur && *it != &t && *it != g_fieldTrack) {
                victim = *it;
                g_loadedOrder.erase(it);
                break;
            }
        }
        if (victim == nullptr) {
            break;
        }
        if (victim->overlay != 0) {
            svc_overlay->remove(mod_ctx, victim->overlay);
        }
        victim->overlay = 0;
        victim->loaded = false;
    }
    return true;
}

static void fail_custom(Z2SeqMgr* seq, const char* why) {
    Track* t = g_cur;
    uint32_t bgm = g_curBgm;
    g_state = State::Idle;
    g_cur = nullptr;
    g_curBgm = 0;
    g_inBattle = false;
    if (t == nullptr) {
        return;
    }
    cm_log_err("echec de la musique custom {} : {} -> retour a la musique d'origine", t->name, why);
    g_internal = true;
    seq->bgmStreamStop(0);
    seq->bgmStart(bgm, 0, 0);  // la musique d'origine reprend
    g_internal = false;
    if (++t->failCount >= 2) {
        t->failed = true;
        cm_log_err("{} a echoue 2 fois : musique d'origine pour le reste de la session", t->name);
    }
}

static void stop_custom(uint32_t fadeTime) {
    if (g_state == State::Idle) {
        return;
    }
    if (g_state == State::Preparing || g_state == State::Playing) {
        if (Z2SeqMgr* seq = g_seq) {
            g_internal = true;
            seq->bgmStreamStop(fadeTime);
            g_internal = false;
        }
    }
    cm_log("musique custom arretee ({})", g_cur ? g_cur->name : std::string("?"));
    g_state = State::Idle;
    g_cur = nullptr;
    g_curBgm = 0;
    g_inBattle = false;
}

// ---------------------------------------------------------------------------
// Melange aleatoire (une graine par sauvegarde)
// ---------------------------------------------------------------------------

static void rebuild_shuffle(uint64_t seed) {
    g_shuffled.clear();
    std::mt19937_64 rng(seed);
    auto shuffle_vec = [&](std::vector<Track*>& v) {
        for (size_t i = v.size(); i > 1; --i) {
            size_t k = static_cast<size_t>(rng() % i);
            std::swap(v[i - 1], v[k]);
        }
    };
    cm_log("melange pour cette sauvegarde (graine {})", seed);
    std::vector<Track*> deck;  // tirage sans remise, remelange quand c'est vide
    for (uint32_t id : kZoneIds) {
        if (g_tracks.count(id)) {
            continue;  // deja fixe par un nom de fichier
        }
        if (deck.empty()) {
            deck = g_pool;
            shuffle_vec(deck);
        }
        Track* t = deck.back();
        deck.pop_back();
        g_shuffled[id] = t;
        cm_log("  {} -> {}", bgm_name(id), t->name);
    }
    g_shuffleSeed = seed;
    g_shuffleValid = true;
}

// Retourne vrai si un melange est disponible pour la sauvegarde en cours.
// La graine est lue dans les donnees de la sauvegarde ; si elle n'existe pas encore
// (nouvelle partie), on en tire une au hasard et on l'y enregistre.
static bool ensure_shuffle() {
    if (g_pool.empty()) {
        return false;
    }
    uint64_t seed = 0;
    bool have = false;
    if (g_forcedSeed) {
        seed = g_forcedSeedValue;
        have = true;
    } else {
        uint64_t stored = 0;
        size_t size = sizeof(stored);
        if (svc_save->get_blob(mod_ctx, "music_seed", &stored, &size) == MOD_OK &&
            size == sizeof(stored) && stored != 0) {
            seed = stored;
            have = true;
        } else {
            std::random_device rd;
            uint64_t fresh = (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd());
            if (fresh == 0) {
                fresh = 1;
            }
            if (svc_save->set_blob(mod_ctx, "music_seed", &fresh, sizeof(fresh)) == MOD_OK) {
                seed = fresh;
                have = true;
                cm_log("nouvelle sauvegarde : graine {} creee", seed);
            }
        }
    }
    if (!have) {
        cm_log_warn("aucune sauvegarde active : pas de melange pour l'instant");
        return false;
    }
    if (!g_shuffleValid || g_shuffleSeed != seed) {
        rebuild_shuffle(seed);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

static HookAction on_bgm_start_pre(ModContext*, void* args, void*, void*) {
    Z2SeqMgr* seq = mods::arg<Z2SeqMgr*>(args, 0);
    g_seq = seq;
    if (g_internal) {
        return HOOK_CONTINUE;
    }
    uint32_t bgmId = mods::arg<uint32_t>(args, 1);
    uint32_t fade = mods::arg<uint32_t>(args, 2);
    cm_log("bgmStart id=0x{:X} nom={} fondu={}", bgmId, bgm_name(bgmId), fade);

    Track* track = nullptr;
    auto it = g_tracks.find(bgmId);
    if (it != g_tracks.end()) {
        track = it->second;  // morceau fixe (nom de fichier = nom du morceau)
    } else if (g_zoneSet.count(bgmId) && ensure_shuffle()) {
        auto sit = g_shuffled.find(bgmId);
        if (sit != g_shuffled.end()) {
            track = sit->second;
        }
    }
    if (track == nullptr) {
        // Musique non remplacee : on coupe la notre si elle joue, le jeu fait le reste.
        stop_custom(fade);
        return HOOK_CONTINUE;
    }
    if (track->failed) {
        return HOOK_CONTINUE;  // ce morceau n'a pas marche : musique d'origine
    }
    if (g_curBgm == bgmId && g_state != State::Idle) {
        return HOOK_SKIP_ORIGINAL;  // deja en train de jouer
    }
    if (!ensure_loaded(*track)) {
        return HOOK_CONTINUE;  // probleme de fichier : la musique d'origine joue
    }

    stop_custom(fade);
    g_internal = true;
    seq->bgmStop(fade, 0);  // coupe la sequence d'origine eventuellement en cours
    g_internal = false;

    g_cur = track;
    g_curBgm = bgmId;
    g_quickDeaths = 0;
    g_playingResume = false;
    g_posBase = 0;
    g_resumeDisabled = false;
    g_inBattle = false;
    g_state = State::Loading;
    cm_log("musique custom demandee : {} (pour {})", track->name, bgm_name(bgmId));
    return HOOK_SKIP_ORIGINAL;  // on n'ouvre pas la sequence d'origine
}

static HookAction on_bgm_stop_pre(ModContext*, void* args, void*, void*) {
    g_seq = mods::arg<Z2SeqMgr*>(args, 0);
    if (g_internal) {
        return HOOK_CONTINUE;
    }
    uint32_t fade = mods::arg<uint32_t>(args, 1);
    cm_log("bgmStop fondu={}", fade);
    stop_custom(fade);
    return HOOK_CONTINUE;
}

static HookAction on_stream_stop_pre(ModContext*, void* args, void*, void*) {
    Z2SeqMgr* seq = mods::arg<Z2SeqMgr*>(args, 0);
    g_seq = seq;
    if (g_internal || g_state == State::Idle || g_cur == nullptr) {
        return HOOK_CONTINUE;
    }
    // Le jeu coupe le stream lui-meme. On le laisse faire (il a peut-etre besoin du
    // stream pour autre chose) : la surveillance s'occupera de relancer notre musique
    // si plus personne n'utilise le stream.
    uint32_t fade = mods::arg<uint32_t>(args, 1);
    cm_log("bgmStreamStop du jeu (fondu={}) pendant {} ; stream actif = 0x{:X}", fade, g_cur->name,
           static_cast<uint32_t>(seq->getStreamBgmID()));
    return HOOK_CONTINUE;
}

// Position estimee (en secondes) dans le morceau en cours de lecture.
static double current_position() {
    if (g_state != State::Playing || g_cur == nullptr) {
        return 0;
    }
    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_startTime).count();
    double pos = g_posBase + elapsed;
    if (!g_playingResume && g_cur->durationSec > 0) {
        pos = std::fmod(pos, g_cur->durationSec);
    }
    return pos;
}

// Le jeu signale un debut de combat : on remplace la musique de zone par un morceau de combat.
static HookAction on_start_battle_pre(ModContext*, void* args, void*, void*) {
    Z2SeqMgr* seq = mods::arg<Z2SeqMgr*>(args, 0);
    g_seq = seq;
    if (g_combat.empty() || g_inBattle || g_state != State::Playing || g_cur == nullptr) {
        return HOOK_CONTINUE;
    }
    Track* pick = nullptr;
    for (int tries = 0; tries < 12 && pick == nullptr; ++tries) {
        Track* c = g_combat[static_cast<size_t>(g_rng() % g_combat.size())];
        if (c->failed || (c == g_lastCombat && g_combat.size() > 1)) {
            continue;
        }
        pick = c;
    }
    if (pick == nullptr || !ensure_loaded(*pick)) {
        return HOOK_CONTINUE;
    }
    g_fieldTrack = g_cur;
    g_fieldPos = current_position();
    cm_log("[combat] debut : {} (la zone {} reprendra a {:.1f} s) ; etats du jeu dist={} seq={} reperage={}",
           pick->name, g_fieldTrack->name, g_fieldPos, static_cast<int>(seq->mBattleDistState),
           static_cast<int>(seq->mBattleSeqState), static_cast<int>(seq->mFlags.mBattleSearched));
    g_internal = true;
    seq->bgmStreamStop(0);
    g_internal = false;
    g_cur = pick;
    g_lastCombat = pick;
    g_playingResume = false;
    g_posBase = 0;
    g_resumeDisabled = false;
    g_quickDeaths = 0;
    g_inBattle = true;
    g_calm = false;
    g_battleStart = std::chrono::steady_clock::now();
    g_state = State::Loading;
    g_wait = 3;
    return HOOK_CONTINUE;
}

// Appele a chaque image pendant un combat : decide quand le combat est fini.
static void check_battle_end(Z2SeqMgr* seq) {
    auto now = std::chrono::steady_clock::now();
    double inBattle = std::chrono::duration<double>(now - g_battleStart).count();
    bool calm = seq->mBattleSeqState == 0 && seq->mBattleDistState == 0 && !seq->mFlags.mBattleSearched;
    if (calm) {
        if (!g_calm) {
            g_calm = true;
            g_calmSince = now;
        }
    } else {
        g_calm = false;
    }
    double calmFor = g_calm ? std::chrono::duration<double>(now - g_calmSince).count() : 0.0;

    const char* why = nullptr;
    if (inBattle > 10.0 && calmFor > 3.0) {
        why = "le jeu n'indique plus de combat";
    } else if (inBattle > 240.0) {
        why = "duree maximale atteinte";
    }
    if (why == nullptr) {
        return;
    }

    Track* field = g_fieldTrack;
    g_inBattle = false;
    cm_log("[combat] fin apres {:.0f} s ({}) -> retour a la musique de zone", inBattle, why);
    g_internal = true;
    seq->bgmStreamStop(0);
    g_internal = false;
    if (field == nullptr || !ensure_loaded(*field)) {
        g_state = State::Idle;
        g_cur = nullptr;
        g_curBgm = 0;
        return;
    }
    g_cur = field;
    g_resumeDisabled = false;
    g_quickDeaths = 0;
    if (!try_resume(*field, g_fieldPos)) {
        g_playingResume = false;
        g_posBase = 0;
    }
    g_state = State::Loading;
    g_wait = 3;
}

static void on_check_bgm_playing_post(ModContext*, void* args, void* retval, void*) {
    // Le jeu demande souvent "est-ce que cette musique joue deja ?". Pour nos
    // morceaux, on repond oui, sinon il relancerait la musique en boucle.
    g_seq = mods::arg<Z2SeqMgr*>(args, 0);
    if (g_state == State::Idle || g_cur == nullptr || retval == nullptr) {
        return;
    }
    uint32_t bgmId = mods::arg<uint32_t>(args, 1);
    if (bgmId == g_curBgm) {
        *static_cast<bool*>(retval) = true;
    }
}

// ---------------------------------------------------------------------------
// Panneau dans le menu des mods
// ---------------------------------------------------------------------------

#if ENABLE_UI_PANEL
static ModResult build_panel(ModContext*, UiElementHandle panel, void*, ModError*) {
    UiElementHandle h = 0;
    svc_ui->pane_add_section(mod_ctx, panel, "Custom Music");
    for (const std::string& line : g_panelLines) {
        svc_ui->pane_add_text(mod_ctx, panel, line.c_str(), &h);
    }
    return MOD_OK;
}
#endif

// ---------------------------------------------------------------------------
// Cycle de vie du mod
// ---------------------------------------------------------------------------

// Declare un fichier .ast au jeu (nouveau stream) et lit sa duree. Retourne nullptr en cas d'echec.
static Track* register_file(const std::string& stem, const std::filesystem::path& path) {
    g_files.emplace_back();
    Track& t = g_files.back();
    t.name = stem;
    t.file = path;
    t.discPath = "/Audiores/Stream/custom_" + std::to_string(g_files.size() - 1) + ".ast";

    ModResult r = svc_audio_res->add_sound_table_stream(mod_ctx, t.discPath.c_str(), nullptr,
                                                        &t.streamHandle, &t.streamId);
    if (r != MOD_OK) {
        cm_log_err("add_sound_table_stream a echoue pour {}", t.name);
        g_files.pop_back();
        return nullptr;
    }

    std::ifstream hf(t.file, std::ios::binary);
    unsigned char h[0x20] = {};
    if (hf.read(reinterpret_cast<char*>(h), sizeof(h))) {
        uint32_t rate = rd32(&h[0x10]);
        uint32_t total = rd32(&h[0x14]);
        if (rate > 0) {
            t.durationSec = static_cast<double>(total) / rate;
        }
    }
    cm_log("fichier : {} ({:.0f} s)", t.name, t.durationSec);
    return &t;
}

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError*) {
    const char* dataDir = nullptr;
    if (svc_host->data_dir(mod_ctx, &dataDir) != MOD_OK || dataDir == nullptr) {
        cm_log_err("impossible d'obtenir le dossier de donnees du mod");
        return MOD_ERROR;
    }
    std::filesystem::path dataPath = path_from_utf8(dataDir);

    std::filesystem::path logPath = dataPath / "custom_music_log.txt";
    g_logPathText = path_to_utf8(logPath);
    g_logFile.open(logPath, std::ios::out | std::ios::trunc);
    cm_log("Custom Music v1.0 : demarrage");

    std::filesystem::path musicDir = dataPath / "music";
    std::error_code ec;
    std::filesystem::create_directories(musicDir, ec);
    g_musicDirText = path_to_utf8(musicDir);
    cm_log("dossier des musiques : {}", g_musicDirText);

    // Table nom (en minuscules) -> ID de musique du jeu.
    std::unordered_map<std::string, uint32_t> byName;
    for (const BgmName& b : kBgmNames) {
        byName[to_lower(b.name)] = b.id;
    }

    // 1. Lister les .ast, tries par nom pour que le melange soit reproductible.
    struct Found {
        std::string stem;
        std::filesystem::path path;
    };
    std::vector<Found> found;
    for (const auto& entry : std::filesystem::directory_iterator(musicDir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (to_lower(path_to_utf8(entry.path().extension())) != ".ast") {
            continue;
        }
        found.push_back({path_to_utf8(entry.path().stem()), entry.path()});
    }
    std::sort(found.begin(), found.end(),
              [](const Found& a, const Found& b) { return to_lower(a.stem) < to_lower(b.stem); });

    // 2. Declarer chaque fichier au jeu ; les noms connus sont "fixes", les autres vont dans la reserve.
    std::vector<Track*> pool;
    size_t pinnedCount = 0;
    for (const Found& fnd : found) {
        std::string key = to_lower(fnd.stem);
        if (key.rfind("z2bgm_", 0) == 0) {
            key = key.substr(6);
        }

        Track* tp = register_file(fnd.stem, fnd.path);
        if (tp == nullptr) {
            continue;
        }
        Track& t = *tp;

        auto nameIt = byName.find(key);
        if (nameIt != byName.end() && !g_tracks.count(nameIt->second)) {
            g_tracks[nameIt->second] = &t;
            ++pinnedCount;
            cm_log("morceau fixe : {} -> {}", bgm_name(nameIt->second), t.name);
        } else {
            pool.push_back(&t);
        }
    }

    // 2b. Musiques de combat : sous-dossier "combat" (n'importe quel nom de fichier).
    {
        std::filesystem::path combatDir = musicDir / "combat";
        std::filesystem::create_directories(combatDir, ec);
        std::vector<Found> combatFound;
        for (const auto& entry : std::filesystem::directory_iterator(combatDir, ec)) {
            if (entry.is_regular_file() &&
                to_lower(path_to_utf8(entry.path().extension())) == ".ast") {
                combatFound.push_back({path_to_utf8(entry.path().stem()), entry.path()});
            }
        }
        std::sort(combatFound.begin(), combatFound.end(),
                  [](const Found& a, const Found& b) { return to_lower(a.stem) < to_lower(b.stem); });
        for (const Found& fnd : combatFound) {
            if (Track* tp = register_file("combat/" + fnd.stem, fnd.path)) {
                g_combat.push_back(tp);
            }
        }
        cm_log("musiques de combat : {}", g_combat.size());
    }

    // Fichiers de reprise (la musique repart ou elle s'est arretee apres une coupure).
    g_resumeAvailable = true;
    for (int i = 0; i < 2; ++i) {
        if (svc_audio_res->add_sound_table_stream(mod_ctx, kResumePath[i], nullptr, &g_resumeStreamHandle[i],
                                                  &g_resumeStreamId[i]) != MOD_OK) {
            g_resumeAvailable = false;
            cm_log_warn("reprise de la musique indisponible (stream {})", i);
        }
    }

    // 3. La reserve sera repartie au hasard, par sauvegarde, au premier besoin.
    g_pool = pool;
    for (uint32_t id : kZoneIds) {
        g_zoneSet.insert(id);
    }
    {
        std::ifstream in(dataPath / "seed_force.txt");
        std::string word;
        if (in && std::getline(in, word)) {
            while (!word.empty() && (word.back() == '\r' || word.back() == ' ' || word.back() == '\n')) {
                word.pop_back();
            }
            if (!word.empty()) {
                // nombre tel quel, sinon empreinte du texte (FNV-1a)
                bool digits = word.size() < 19;
                for (char c : word) {
                    digits = digits && std::isdigit(static_cast<unsigned char>(c));
                }
                uint64_t v = 1469598103934665603ull;
                if (digits) {
                    v = std::stoull(word);
                } else {
                    for (unsigned char c : word) {
                        v = (v ^ c) * 1099511628211ull;
                    }
                }
                g_forcedSeed = true;
                g_forcedSeedValue = v == 0 ? 1 : v;
                cm_log("graine imposee par seed_force.txt : {} ({})", word, g_forcedSeedValue);
            }
        }
    }

    ModResult result = mods::hook::add_pre<SeqBgmStart>(on_bgm_start_pre);
    if (result != MOD_OK) {
        cm_log_err("echec du hook bgmStart");
        return result;
    }
    result = mods::hook::add_pre<SeqBgmStop>(on_bgm_stop_pre);
    if (result != MOD_OK) {
        cm_log_err("echec du hook bgmStop");
        return result;
    }
    result = mods::hook::add_pre<SeqBgmStreamStop>(on_stream_stop_pre);
    if (result != MOD_OK) {
        cm_log_err("echec du hook bgmStreamStop");
        return result;
    }
    result = mods::hook::add_pre<SeqStartBattleBgm>(on_start_battle_pre);
    if (result != MOD_OK) {
        cm_log_warn("hook startBattleBgm indisponible : pas de musique de combat");
    }
    result = mods::hook::add_post<SeqCheckBgmIDPlaying>(on_check_bgm_playing_post);
    if (result != MOD_OK) {
        cm_log_err("echec du hook checkBgmIDPlaying");
        return result;
    }

    g_panelLines.push_back("Dossier des musiques : " + g_musicDirText);
    g_panelLines.push_back("Journal de diagnostic : " + g_logPathText);
    g_panelLines.push_back("Fichiers .ast trouves : " + std::to_string(g_files.size()));
    g_panelLines.push_back("Morceaux fixes (nom de fichier = nom du morceau du jeu) : " +
                           std::to_string(pinnedCount));
    g_panelLines.push_back("Musiques de combat (sous-dossier combat) : " + std::to_string(g_combat.size()));
    g_panelLines.push_back("Reserve aleatoire : " + std::to_string(pool.size()) +
                           " fichier(s), repartis sur les zones du jeu");
    g_panelLines.push_back(g_forcedSeed
        ? std::string("Melange : graine imposee par seed_force.txt (identique pour toutes les sauvegardes)")
        : std::string("Melange : une graine differente par sauvegarde (nouvelle partie = nouveau melange)"));

#if ENABLE_UI_PANEL
    UiModsPanelDesc panel = UI_MODS_PANEL_DESC_INIT;
    panel.build = build_panel;
    svc_ui->register_mods_panel(mod_ctx, &panel);
#endif

    cm_log("Custom Music initialise : {} fichier(s), {} fixe(s), {} dans la reserve", g_files.size(),
           pinnedCount, pool.size());
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    if (g_state == State::Idle || g_cur == nullptr) {
        return MOD_OK;
    }
    Z2SeqMgr* seq = g_seq;
    if (seq == nullptr) {
        return MOD_OK;
    }
    if (g_inBattle) {
        check_battle_end(seq);
        if (g_state == State::Idle || g_cur == nullptr) {
            return MOD_OK;
        }
    }

    switch (g_state) {
    case State::Loading:
        if (--g_wait <= 0) {
            uint32_t sid = current_sound_id(*g_cur);
            seq->bgmStreamPrepare(sid);
            cm_log("bgmStreamPrepare(0x{:X}) appele ; id du stream actif = 0x{:X}", sid,
                 static_cast<uint32_t>(seq->getStreamBgmID()));
            g_state = State::Preparing;
            g_wait = 0;
        }
        break;
    case State::Preparing:
        if (seq->bgmStreamCheckReady()) {
            seq->bgmStreamPlay();
            cm_log("bgmStreamPlay appele ; id du stream actif = 0x{:X}",
                 static_cast<uint32_t>(seq->getStreamBgmID()));
            g_state = State::Playing;
            g_wait = 0;
            g_startTime = std::chrono::steady_clock::now();
        } else {
            ++g_wait;
            if (g_wait % 60 == 0) {
                cm_log("en attente du stream... {} images, id du stream actif = 0x{:X}", g_wait,
                     static_cast<uint32_t>(seq->getStreamBgmID()));
            }
            if (g_wait > 300) {
                fail_custom(seq, "le stream n'est jamais devenu pret");
            }
        }
        break;
    case State::Playing: {
        ++g_wait;
        uint32_t active = static_cast<uint32_t>(seq->getStreamBgmID());
        uint32_t mine = current_sound_id(*g_cur);
        if (g_wait == 60 || g_wait == 300) {
            cm_log("{} images apres le lancement : musique en cours = {}, id du stream actif = 0x{:X}",
                   g_wait, static_cast<bool>(seq->checkBgmPlaying()), active);
        }
        // Surveillance : le stream est-il toujours le notre ?
        if (g_wait > 20 && active != mine) {
            double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - g_startTime).count();
            if (active == 0xFFFFFFFFu) {
                // Le stream est termine ou a ete coupe (changement de salle...). On le relance,
                // si possible a la position ou il s'est arrete. S'il meurt tout de suite
                // plusieurs fois de suite, c'est un vrai probleme : on abandonne.
                const double dur = g_cur->durationSec;
                const bool wasResume = g_playingResume;
                const bool tailDone = wasResume && g_tailSec > 0 && elapsed >= g_tailSec - 2.0;
                const double pos = g_posBase + elapsed;
                if (tailDone) {
                    cm_log("fin de la reprise de {} apres {:.1f} s -> on repart sur le morceau complet",
                           g_cur->name, elapsed);
                } else {
                    cm_log("interruption de {} apres {:.1f} s (position : {:.1f} s sur {:.1f} s)",
                           g_cur->name, elapsed, (dur > 0 && !wasResume) ? std::fmod(pos, dur) : pos, dur);
                }
                g_quickDeaths = elapsed < 3.0 ? g_quickDeaths + 1 : 0;
                if (g_quickDeaths >= 3) {
                    fail_custom(seq, "le stream s'arrete immediatement, a chaque relance");
                } else {
                    bool resumed = false;
                    if (!tailDone && !g_resumeDisabled) {
                        if (wasResume && elapsed < 3.0) {
                            g_resumeDisabled = true;
                            cm_log_warn("la reprise ne fonctionne pas : retour au morceau complet");
                        } else {
                            double posMod = (dur > 0 && !wasResume) ? std::fmod(pos, dur) : pos;
                            resumed = try_resume(*g_cur, posMod);
                        }
                    }
                    if (!resumed) {
                        g_playingResume = false;
                        g_posBase = 0;
                    }
                    g_state = State::Loading;
                    g_wait = resumed ? 3 : 0;
                }
            } else {
                cm_log_warn("un autre stream (0x{:X}) a pris la place de {}", active, g_cur->name);
                g_state = State::Idle;
                g_cur = nullptr;
                g_curBgm = 0;
                g_inBattle = false;
            }
        }
        break;
    }
    default:
        break;
    }
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    g_state = State::Idle;  // pas d'appel au jeu ici : il est peut-etre deja ferme
    g_cur = nullptr;
    g_curBgm = 0;
    g_seq = nullptr;
    for (Track& t : g_files) {
        svc_audio_res->remove_sound_table(mod_ctx, t.streamHandle);
    }
    for (int i = 0; i < 2; ++i) {
        if (g_resumeStreamHandle[i] != 0) {
            svc_audio_res->remove_sound_table(mod_ctx, g_resumeStreamHandle[i]);
        }
    }
    g_loadedOrder.clear();
    g_tracks.clear();
    g_shuffled.clear();
    g_pool.clear();
    g_shuffleValid = false;
    g_files.clear();
    if (g_logFile.is_open()) {
        g_logFile.close();
    }
    return MOD_OK;
}
}
