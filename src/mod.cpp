// Dusklight Custom Music - version 0.1 (experimentale)
//
// Remplace des musiques de fond du jeu (sequences) par tes propres fichiers .ast.
// Depose tes fichiers dans le dossier "music" du mod (son chemin s'affiche dans
// le panneau des mods, et dans le journal au demarrage). Le nom du fichier doit
// etre le nom du morceau du jeu, par exemple FIELD_LINK_DAY.ast
//
// Principe :
//   1. au demarrage, on cherche les .ast du dossier "music" et on les declare
//      au jeu comme de nouveaux streams (AudioResService) ;
//   2. quand le jeu demande une musique remplacee (Z2SeqMgr::bgmStart), on
//      annule la sequence d'origine et on lance a la place la musique en stream
//      avec le systeme de musique streamee du jeu (bgmStreamPrepare / Play) ;
//   3. quand le jeu arrete la musique (Z2SeqMgr::bgmStop), on arrete le stream.

#include "mods/service.hpp"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.hpp"
#include "mods/svc/host.h"
#include "mods/svc/overlay.h"
#include "mods/svc/audio_res.h"
#include "mods/svc/ui.h"

// Game includes
#include "Z2AudioLib/Z2SeqMgr.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

// Mettre 0 si la compilation echoue a cause du panneau d'interface.
#define ENABLE_UI_PANEL 1

DEFINE_MOD();

IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(HostService, svc_host);
IMPORT_SERVICE(OverlayService, svc_overlay);
IMPORT_SERVICE(AudioResService, svc_audio_res);
#if ENABLE_UI_PANEL
IMPORT_SERVICE(UiService, svc_ui);
#endif

DEFINE_HOOK(&Z2SeqMgr::bgmStart, SeqBgmStart);
DEFINE_HOOK(&Z2SeqMgr::bgmStop, SeqBgmStop);
DEFINE_HOOK(&Z2SeqMgr::checkBgmIDPlaying, SeqCheckBgmIDPlaying);

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
    std::string name;       // ex: FIELD_LINK_DAY
    uint32_t bgmId = 0;     // ex: Z2BGM_FIELD_LINK_DAY
    std::filesystem::path file;
    std::string discPath;   // chemin virtuel donne au jeu
    uint16_t streamId = 0;  // ID attribue par AudioResService
    AudioSoundTableHandle streamHandle = 0;
    bool loaded = false;    // le fichier a deja ete envoye au jeu (overlay)
};

enum class State { Idle, Loading, Preparing, Playing };

static std::unordered_map<uint32_t, Track> g_tracks;  // cle = ID de la musique du jeu
static State g_state = State::Idle;
static Track* g_cur = nullptr;
static int g_wait = 0;
static bool g_internal = false;  // vrai quand c'est NOUS qui appelons bgmStop
static std::string g_musicDirText;
static std::vector<std::string> g_panelLines;

static uint32_t stream_sound_id(const Track& t) {
    // JAISoundID : section 2 (musique streamee), groupe 0, puis l'ID du stream.
    return (2u << 24) | t.streamId;
}

// Envoie le fichier .ast au jeu (en memoire). Fait une seule fois par morceau.
static bool ensure_loaded(Track& t) {
    if (t.loaded) {
        g_wait = 0;
        return true;
    }
    std::ifstream f(t.file, std::ios::binary | std::ios::ate);
    if (!f) {
        mods::log::error("impossible d'ouvrir {}", path_to_utf8(t.file));
        return false;
    }
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (size > 0 && !f.read(reinterpret_cast<char*>(buf.data()), size)) {
        mods::log::error("erreur de lecture de {}", path_to_utf8(t.file));
        return false;
    }
    ModResult r = svc_overlay->add_buffer(mod_ctx, t.discPath.c_str(), buf.data(), buf.size(), nullptr);
    if (r != MOD_OK) {
        mods::log::error("overlay add_buffer a echoue pour {}", t.name);
        return false;
    }
    t.loaded = true;
    g_wait = 3;  // l'overlay est applique au prochain "frame boundary" : on attend un peu
    return true;
}

static void stop_custom(uint32_t fadeTime) {
    if (g_state == State::Idle) {
        return;
    }
    if (g_state == State::Preparing || g_state == State::Playing) {
        if (Z2SeqMgr* seq = Z2GetSeqMgr()) {
            seq->bgmStreamStop(fadeTime);
        }
    }
    mods::log::info("musique custom arretee ({})", g_cur ? g_cur->name : std::string("?"));
    g_state = State::Idle;
    g_cur = nullptr;
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

static HookAction on_bgm_start_pre(ModContext*, void* args, void*, void*) {
    if (g_internal) {
        return HOOK_CONTINUE;
    }
    Z2SeqMgr* seq = mods::arg<Z2SeqMgr*>(args, 0);
    uint32_t bgmId = mods::arg<uint32_t>(args, 1);
    uint32_t fade = mods::arg<uint32_t>(args, 2);
    mods::log::info("bgmStart id=0x{:X} nom={} fondu={}", bgmId, bgm_name(bgmId), fade);

    auto it = g_tracks.find(bgmId);
    if (it == g_tracks.end()) {
        // Musique non remplacee : on coupe la notre si elle joue, le jeu fait le reste.
        stop_custom(fade);
        return HOOK_CONTINUE;
    }
    if (g_cur == &it->second && g_state != State::Idle) {
        return HOOK_SKIP_ORIGINAL;  // deja en train de jouer
    }
    if (!ensure_loaded(it->second)) {
        return HOOK_CONTINUE;  // probleme de fichier : la musique d'origine joue
    }

    stop_custom(fade);
    g_internal = true;
    seq->bgmStop(fade, 0);  // coupe la sequence d'origine eventuellement en cours
    g_internal = false;

    g_cur = &it->second;
    g_state = State::Loading;
    mods::log::info("musique custom demandee : {}", it->second.name);
    return HOOK_SKIP_ORIGINAL;  // on n'ouvre pas la sequence d'origine
}

static HookAction on_bgm_stop_pre(ModContext*, void* args, void*, void*) {
    if (g_internal) {
        return HOOK_CONTINUE;
    }
    uint32_t fade = mods::arg<uint32_t>(args, 1);
    mods::log::info("bgmStop fondu={}", fade);
    stop_custom(fade);
    return HOOK_CONTINUE;
}

static void on_check_bgm_playing_post(ModContext*, void* args, void* retval, void*) {
    // Le jeu demande souvent "est-ce que cette musique joue deja ?". Pour nos
    // morceaux, on repond oui, sinon il relancerait la musique en boucle.
    if (g_state == State::Idle || g_cur == nullptr || retval == nullptr) {
        return;
    }
    uint32_t bgmId = mods::arg<uint32_t>(args, 1);
    if (bgmId == g_cur->bgmId) {
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

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError*) {
    const char* dataDir = nullptr;
    if (svc_host->data_dir(mod_ctx, &dataDir) != MOD_OK || dataDir == nullptr) {
        mods::log::error("impossible d'obtenir le dossier de donnees du mod");
        return MOD_ERROR;
    }

    std::filesystem::path musicDir = path_from_utf8(dataDir) / "music";
    std::error_code ec;
    std::filesystem::create_directories(musicDir, ec);
    g_musicDirText = path_to_utf8(musicDir);
    mods::log::info("dossier des musiques : {}", g_musicDirText);

    // Table nom (en minuscules) -> ID de musique.
    std::unordered_map<std::string, uint32_t> byName;
    for (const BgmName& b : kBgmNames) {
        byName[to_lower(b.name)] = b.id;
    }

    std::vector<std::string> ignored;
    for (const auto& entry : std::filesystem::directory_iterator(musicDir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string ext = to_lower(path_to_utf8(entry.path().extension()));
        if (ext != ".ast") {
            continue;
        }
        std::string stem = to_lower(path_to_utf8(entry.path().stem()));
        if (stem.rfind("z2bgm_", 0) == 0) {
            stem = stem.substr(6);
        }
        auto nameIt = byName.find(stem);
        if (nameIt == byName.end()) {
            ignored.push_back(path_to_utf8(entry.path().filename()));
            mods::log::warn("nom de morceau inconnu, fichier ignore : {}", ignored.back());
            continue;
        }
        if (g_tracks.count(nameIt->second)) {
            mods::log::warn("deux fichiers pour le meme morceau, ignore : {}", stem);
            continue;
        }

        Track& t = g_tracks[nameIt->second];
        t.name = stem;
        t.bgmId = nameIt->second;
        t.file = entry.path();
        t.discPath = "/Audiores/Stream/custom_" + stem + ".ast";

        ModResult r = svc_audio_res->add_sound_table_stream(
            mod_ctx, t.discPath.c_str(), nullptr, &t.streamHandle, &t.streamId);
        if (r != MOD_OK) {
            mods::log::error("add_sound_table_stream a echoue pour {}", stem);
            g_tracks.erase(nameIt->second);
            continue;
        }
        mods::log::info("morceau custom pret : {} (stream id {})", stem, t.streamId);
    }

    ModResult result = mods::hook::add_pre<SeqBgmStart>(on_bgm_start_pre);
    if (result != MOD_OK) {
        mods::log::error("echec du hook bgmStart");
        return result;
    }
    result = mods::hook::add_pre<SeqBgmStop>(on_bgm_stop_pre);
    if (result != MOD_OK) {
        mods::log::error("echec du hook bgmStop");
        return result;
    }
    result = mods::hook::add_post<SeqCheckBgmIDPlaying>(on_check_bgm_playing_post);
    if (result != MOD_OK) {
        mods::log::error("echec du hook checkBgmIDPlaying");
        return result;
    }

    g_panelLines.push_back("Dossier des musiques : " + g_musicDirText);
    g_panelLines.push_back("Morceaux detectes : " + std::to_string(g_tracks.size()));
    for (auto& kv : g_tracks) {
        g_panelLines.push_back(" - " + kv.second.name);
    }
    for (const std::string& name : ignored) {
        g_panelLines.push_back("Nom inconnu (ignore) : " + name);
    }
    g_panelLines.push_back("Ajoute un fichier NOM_DU_MORCEAU.ast dans ce dossier puis relance le jeu.");

#if ENABLE_UI_PANEL
    UiModsPanelDesc panel = UI_MODS_PANEL_DESC_INIT;
    panel.build = build_panel;
    svc_ui->register_mods_panel(mod_ctx, &panel);
#endif

    mods::log::info("Custom Music initialise : {} morceau(x)", g_tracks.size());
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    if (g_state == State::Idle || g_cur == nullptr) {
        return MOD_OK;
    }
    Z2SeqMgr* seq = Z2GetSeqMgr();
    if (seq == nullptr) {
        return MOD_OK;
    }

    switch (g_state) {
    case State::Loading:
        if (--g_wait <= 0) {
            seq->bgmStreamPrepare(stream_sound_id(*g_cur));
            g_state = State::Preparing;
            g_wait = 0;
        }
        break;
    case State::Preparing:
        if (seq->bgmStreamCheckReady()) {
            seq->bgmStreamPlay();
            g_state = State::Playing;
            mods::log::info("musique custom en lecture : {}", g_cur->name);
        } else if (++g_wait > 900) {
            mods::log::error("le stream {} n'est jamais devenu pret (abandon)", g_cur->name);
            g_state = State::Idle;
            g_cur = nullptr;
        }
        break;
    default:
        break;
    }
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    stop_custom(0);
    for (auto& kv : g_tracks) {
        svc_audio_res->remove_sound_table(mod_ctx, kv.second.streamHandle);
    }
    g_tracks.clear();
    return MOD_OK;
}
}
