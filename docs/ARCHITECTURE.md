# Architecture

```text
Application de référence / futures applications
                │
  cyvr_ui : DrawList, UiModel, Settings, FrameStats
                │                         │
  géométrie : PanelGeometry          SpatialMesh
                │                         │
  cyvr_gpu : atlas + pipeline 2D + pipeline 3D
                │
   Android par défaut : PanelActivity → Vulkan WSI → fenêtre 2D Horizon OS
   Android facultatif : ImmersiveActivity → OpenXR → runtime du casque
   Windows : cible Vulkan hors écran → fenêtre Win32
```

Le socle est une UI 2D commune. `cyvr_android` choisit son hôte à partir de l’activité : le panneau natif ne crée aucune session OpenXR. L’immersion est facultative et peut être exclue à la compilation. Voir [Android hosts and migration](ANDROID_HOSTS.md).

## Fichiers

| Fichier | Rôle |
|---|---|
| `include/cyvr/core.hpp`, `src/core.cpp` | Types, rayons, panneaux, texte UTF-8, maillage UI, sélection/capture, modèle de référence, statistiques |
| `src/ui.cpp` | Pages et style du catalogue, contrôles et cibles interactives |
| `include/cyvr/spatial.hpp`, `src/spatial.cpp` | Matrices de vue/projection, géométrie des mains, articulations et lasers |
| `include/cyvr/gpu.hpp`, `src/gpu.cpp` | Ressources Vulkan, atlas, synchronisation, pipelines 2D/3D et captures PC |
| `src/android_host.cpp` | Hôte 2D Android, surface/swapchain Vulkan, cycle de vie, redimensionnement et entrée système |
| `src/xr_app.cpp` | Session OpenXR, entrées, capacités, swapchains, placement et composition |
| `src/android_main.cpp` | Point d’entrée NativeActivity, attachement JNI et traitement d’erreur |
| `src/desktop.cpp` | Inspecteur Windows natif utilisant le même cœur et le même backend Vulkan |
| `include/cyvr/storage.hpp` | Préférences et export JSON |
| `assets/fonts` | Police originale et licence OFL |
| `assets/font_atlas.bin`, `include/cyvr/font_data.hpp` | Atlas R8 et métriques, régénérables par `tools/bake_font.py` |

## Rendu

En mode 2D, l’interface est présentée dans une surface Android native via `VK_KHR_android_surface` et `VK_KHR_swapchain`. Les commandes de fenêtre appartiennent au système. Le contenu est ajusté au ratio de la fenêtre ; les coordonnées d’entrée suivent la transformation inverse. Aucun `xrCreateInstance` ou `xrCreateSession` n’est appelé par cet hôte.

En mode immersif facultatif, l’interface principale est une couche quadrilatère OpenXR, avec sa propre swapchain Vulkan sRGB. Le runtime assure la présentation stéréo du panneau. La texture est réutilisée tant que le contenu ne change pas. Le panneau diagnostic et les réticules utilisent d’autres couches.

Les mains et lasers utilisent des normales lissées, un éclairage simple et une profondeur D32. Deux vues OpenXR les projettent dans une couche qui dessine également le décor natif. Leur résolution est plafonnée à 1440 pixels sur le grand côté dans l’application de référence ; les textes restent dans leur couche de panneau indépendante. Les ressources GPU sont créées au démarrage.

Les décors Salon lumineux et Venise au crépuscule utilisent des panoramas photographiques CC0 de 4096 × 2048 pixels, échantillonnés par les rayons en espace monde. Le chargement JPEG et le transfert GPU ont lieu à l’entrée en immersion, sans réseau ni lecture GPU vers le CPU. Les deux textures sRGB occupent environ 64 Mio ; l’hôte 2D ne les charge pas. Ce sont des fonds monoscopiques à 360°, sans parallaxe de translation ; les mains et panneaux restent en 3D stéréo. Sans ces ressources facultatives, le décor procédural léger est conservé. Le verre dépoli évalue un filtre gaussien 3 × 3 sur le même décor dans une cible 480 × 330, puis l’échantillonne linéairement sous l’UI. Le contenu UI conserve sa géométrie tant que sa révision ne change pas ; le fond de verre s’actualise lors d’un déplacement du point de vue, au maximum 30 Hz hors modifications de contenu. Le texte est dessiné après le flou et reste opaque. Les surfaces suivent l’opacité réglée. Avec le passthrough, le matériau devient transparent teinté sans filtrer l’image du casque.

Ordre des couches : passthrough facultatif, projection décor/mains/rayons, couches applicatives, fenêtre principale, HUD, barre flottante, réticules. Si le runtime expose `XR_KHR_composition_layer_depth`, `XR_FB_composition_layer_depth_test` et le format D32, la projection soumet une swapchain de profondeur par œil avec les mêmes dimensions et plans proche/lointain que le rendu Vulkan. Les indices couleur et profondeur sont acquis indépendamment. Le render pass conserve la profondeur ; seules les images possédées par le renderer sont détruites par lui.

La projection écrit la profondeur de ses mains et rayons dans le compositeur. `DepthComposition` ajoute aux couches quad/cylindre un test LESS_OR_EQUAL en conservant leur chaîne `next` et les politiques déjà définies par les applications. Ces panneaux ne réécrivent pas la profondeur, ce qui évite que leurs coins transparents masquent les autres couches. Une main plus proche peut ainsi recouvrir le panneau, tandis qu’une main plus éloignée reste derrière. Sans ces capacités, l’ancien ordre de composition est conservé avec un message explicite dans le journal ; une validation visuelle au casque reste nécessaire.

Le backend utilise une file graphique et un buffer de commande réutilisé avec une fence. Cette première version attend explicitement la fin des mises à jour avant de relâcher les images à OpenXR. Cela simplifie la propriété des ressources ; l’ajout de plusieurs images en vol demandera du profilage avant modification.

La référence Windows effectue une lecture GPU uniquement pour l’inspection sur écran plat. **Le chemin Quest ne fait pas cette lecture.**

## Scènes 3D originales

`scene.hpp` et `scene.cpp` construisent trois décors en mètres, avec le sol à Y=0 : salon moderne, observatoire et terrasse. Les valeurs historiques Studio=0, Night=1 et Passthrough=2 sont conservées ; les scènes 3D ajoutent les valeurs 3 à 5. Le sélecteur immersif utilise six choix traduits ; le panneau Android 2D garde ses décors légers.

Les primitives biseautées, plantes, mobilier et paysages sont des créations originales sous MIT. Le générateur calcule une approximation d’occlusion ambiante et d’ombres de soleil à partir des volumes de boîtes lors de la sélection. Les sommets portent le matériau, l’occlusion ambiante et la visibilité du soleil ; les shaders ajoutent un éclairage simple, des détails analytiques filtrés et une brume distante. Aucun shadow map dynamique ni modèle tiers n’est nécessaire.

Le renderer conserve un seul buffer statique de scène, entre 1,06 et 1,70 Mio pour les décors inclus. Il effectue deux dessins de géométrie par œil : les surfaces fermées avec élimination des faces arrière, puis les surfaces ouvertes (anneaux plats, feuilles) et les sources lumineuses, dont les faces arrière masquent les jointures fines. Ce tri a lieu au chargement, après l’éclairage précalculé qui dépend de l’ordre d’origine. Le ciel est dessiné ensuite avec un test de profondeur, puis les mains avec la même profondeur D32. La géométrie ne repasse pas par le CPU à chaque image. L’ancrage au sol utilise LOCAL_FLOOR si l’extension est disponible, sinon STAGE, avec une estimation de 1,65 m sous la vue initiale si le sol est indisponible. Les changements d’origine connus sont appliqués à leur échéance pour préserver la pose du décor. Le déplacement du panneau reste indépendant du décor.

Pour le verre dépoli, `rectangleProjection` construit une projection hors axe à travers le panneau. Le décor réel est dessiné dans une cible 480 × 330 avec profondeur, puis filtré avant le texte. Cette cible n’est créée que lors de l’utilisation d’une scène 3D ; aucune lecture GPU n’est faite sur Quest. Les panoramas sont désormais chargés uniquement à leur première sélection, afin qu’un usage exclusivement 3D ne consomme pas leurs 64 Mio de textures.

## Entrées et placement

Les actions OpenXR sont séparées des contrôles visuels. Les manettes utilisent le profil Oculus Touch standard : aim/grip, gâchettes et haptique. Le suivi des mains est optionnel, vérifié par capacité ; il utilise `XR_EXT_hand_tracking`, avec `XR_FB_hand_tracking_aim` si disponible et un repli géométrique sinon.

Un rayon est transformé dans le repère du panneau pour obtenir des coordonnées UI. L’appui capture le contrôle ; le relâchement valide seulement la cible correspondante. Les curseurs conservent la capture lorsqu’on sort de leur rectangle. Les modales remplacent les cibles interactives de fond. Deux pointeurs sont pris en charge.

Les poignées supérieure et flottante capturent un pointeur et conservent la profondeur de saisie. Elles déplacent la fenêtre, le HUD et la barre, avec une distance bornée entre 0,8 et 3 mètres par rapport à la tête. La fenêtre se tourne vers l’utilisateur au relâchement. Les hit tests des deux surfaces sont comparés en profondeur ; la barre a ses propres coordonnées virtuelles sous le panneau principal.

Trois états de fenêtre : ouverte, réduite (page conservée), fermée (retour à l’accueil). La barre reste disponible pour restaurer ou rouvrir. Les hit regions du contenu sont retirées pendant les deux états masqués. Trois modes de placement : monde, orientation vers l’utilisateur sans translation, suivi amorti de la tête. La fenêtre regarde vers la tête avec une inclinaison bornée à 45°, sans roulis ; le suivi se fige pendant un appui et saisir une fenêtre suivie rétablit l’ancrage monde. Les thèmes, matériaux, modèles et préférences de suivi sont persistés localement.

Le panneau mesure par défaut 1,20 × 0,825 m à 1,35 m, pour une texture de 1440 × 990 pixels. Les boutons principaux utilisent des cibles d’environ 5 cm. Les petits textes secondaires et le confort doivent être évalués sur chaque casque.

## Extension

`renderApplicationDock` permet aussi de personnaliser la barre flottante en conservant la suppression des cibles du panneau lorsqu’il est masqué. Le rendu optionnel d’images passe par `DrawList::image(rect, uv, tint, radius)` et un atlas RGBA téléversé par `VulkanRenderer::uploadImages` (dimensions maximales 2048 × 2048). L’atlas de police reste indépendant ; une image neutre est créée par défaut pour les applications sans textures supplémentaires. Les applications doivent charger les images entre deux rendus, sur le thread du renderer. CyVR_Launcher utilise ce chemin pour les icônes Android, avec un transfert au rafraîchissement du catalogue.

Une application peut fournir `UiModel::renderApplication(DrawList&)` pour remplacer le panneau complet et `handleApplicationAction(int)` pour traiter ses actions. Renvoyer `false` conserve le traitement du catalogue. `applicationWidget(...)` enregistre les cibles dans le système commun de pointeurs ; `clearApplicationWidgets()` les retire lors d’un nouveau dessin. Les identifiants 600–699 sont réservés au dock spatial ; pour de nouvelles pages métier, utiliser 1000 et plus afin d’éviter également les contrôles du catalogue. `toast()` expose le retour d’état à un panneau personnalisé. Annuler les captures avec `navigate(page)` lors d’un changement de collection ou de page, puis détacher les callbacks avant de détruire leur contrôleur. CyVR_Launcher utilise ces points d’extension tout en réutilisant directement les moteurs du socle.

`renderContent(DrawList&)` remplace seulement la zone de contenu, tout en gardant l’en-tête, la navigation, les modales et le pied de panneau. Les libellés des pages et les textes de confirmation sont configurables dans `UiModel`. `applicationTick(seconds)` permet de consulter un état applicatif déjà mis en cache ; éviter toute opération bloquante dans ce callback. Sur Android, `runAndroidApp(app, AndroidAppConfig)` accepte un nom, une version et un callback `configure(UiModel&)`, exécuté après le chargement des préférences. CyVR Optimizer utilise ce chemin et conserve ses accès aux profils et à Binder dans son propre contrôleur Java.

Le catalogue est un exemple, pas un gestionnaire universel de fenêtres. Ajouter une page revient à dessiner ses éléments, enregistrer leurs rectangles et traiter leurs identifiants d’action dans le modèle. Pour un produit plus vaste, extraire un routeur de pages, des composants métier et un gestionnaire de fenêtres au-dessus des bibliothèques existantes, sans déplacer la logique métier dans Vulkan ou OpenXR.

Les palettes Graphite, Clair et Océan sont définies par `palette(Settings)` dans `src/ui.cpp`. Les icônes partagent une grille de 24 unités et sont dessinées par les primitives vectorielles du moteur. Aucun fichier web ni fonte d’icônes n’est utilisé. Sans MSAA, `DrawList::rect`, `border`, `circle`, `ring` et `polyline` restent nets à toute résolution : `ui.frag` lit une distance dans les coordonnées de texture (`u >= 8`), sans attribut de sommet supplémentaire. Les images gardent une géométrie simple, car leurs UV dérivent des positions. Les décors photographiques facultatifs et leurs sources CC0 sont documentés dans `assets/scenes/README.md`.

## Modèles de mains

Le modèle Studio est original : `tools/bake_hands.py` construit une union implicite lissée, extraite par tétraèdres. Le fichier `assets/hand_studio.bin` utilise un en-tête de 12 octets (`CYRH`, uint32 version 1, uint32 nombre de sommets), puis des sommets position XYZ et normale XYZ en float32 little endian, triangles non indexés. Le repère de fabrication est +Y vers le dos de la main, +Z vers le poignet, −X vers le pouce. `controllerHand` effectue le miroir gauche/droite et la conversion vers le repère OpenXR grip, puis applique l’échelle et l’inclinaison. Le fichier actuel contient 17 484 triangles. Un `HandSurface` alternatif peut être injecté dans cet appel ou remplacer la ressource au build.

Pour les mains nues, le mode automatique obtient une seule fois le maillage `XR_FB_hand_tracking_mesh`, conservé en mémoire sans export. À chaque image, `skinnedHand` applique les poses de joints multipliées par l’inverse des poses de liaison et l’échelle mesurée `XrHandTrackingScaleFB`. `HandSkin` accepte aussi un modèle fourni par l’application : positions et normales, indices triangulaires, 26 poses de liaison et jusqu’à quatre influences normalisées par sommet. Les articulations utilisent l’ordre `XR_HAND_JOINT_SET_DEFAULT_EXT`. Les modèles importés doivent posséder leur propre licence de redistribution ; seul le gant original est inclus ici.

Les modes Studio et Squelette peuvent également dessiner les joints suivis sans utiliser le maillage du runtime. Masquées ne modifie ni le suivi ni la sélection. Les modèles des manettes restent une approximation de pose, pas un suivi des doigts.

## Matériaux PBR des scènes immersives (0.7.0)

`src/pbr.cpp` décrit les ressources binaires vérifiées, les lumières HDR et les replis neutres ; `tools/prepare_pbr.py` fabrique les ressources originales. `SceneVertex` occupe 80 octets : position/couleur/normale, identifiant de matériau et AO, UV, tangente et lumière diffuse indirecte. Les faces étendues reçoivent davantage de sommets pour l’éclairage précalculé. Le bake de la 0.7.0 utilisait des boîtes et des points lumineux espacés ; ses taches visibles ont conduit au remplacement de la 0.7.1. Désormais, `tools/bake_scene_lighting.cpp` calcule hors ligne un BVH sur les vrais triangles, 512 chemins par échantillon unique, deux interactions diffuses au maximum, 2 048 échantillons de surfaces émissives et 64 échantillons du soleil. Un filtre coplanaire réduit le bruit résiduel. `applySceneLighting` vérifie les fichiers `CYLI` (scène, nombre de sommets, taille exacte, valeurs finies) avant de remplir les attributs existants. Le casque ne lance aucun rayon et ne calcule pas ce bake au changement de scène. Les détails d’ombre restent limités par l’échantillonnage des sommets ; il ne s’agit pas d’un atlas de lightmaps haute résolution. Les anciennes ellipses peintes sous les canapés ont été retirées.

`scene.frag` utilise une BRDF métal/rugosité GGX avec Fresnel de Schlick et visibilité de Smith corrélée. Les bindings 8 et 9 contiennent neuf couches de couleurs sRGB/AO et normales XY/rugosité/métal ; le binding 10 reçoit une cubemap HDR RGBA16F à huit niveaux de rugosité ; le binding 11 reçoit la table BRDF RG16F. Les bindings 0 à 7 des interfaces, vidéos, applications et panoramas sont conservés. Les lumières et matériaux sont évalués en espace linéaire, puis tonemappés vers la cible sRGB. Les mipmaps et une correction de variance spéculaire limitent le scintillement.

`VulkanRenderer::setScene(background, anchor, reader)` charge les matériaux une fois, puis remplace seulement la sonde au changement de scène. Il ne conserve pas le callback de lecture. Sans ressources facultatives, des textures neutres très petites permettent encore le rendu. Les applications peuvent activer `CYVR_INCLUDE_PBR` pour copier les ressources de référence ou les fournir dans leur propre AssetReader ; une application strictement 2D ne les charge pas. Le verre dépoli reçoit le même rendu PBR de la scène. Les sondes restent statiques, sans reflets en temps réel des mains ou fenêtres.

## Vue AR optionnelle (0.8.0)

Le contrôle caméra 412 et le choix 435 passent par `UiModel::setAugmentedReality`. La sélection courante reste `Background::Passthrough` (ID sauvegardé 2) ; `virtualBackground` mémorise le dernier décor virtuel, y compris les modes Noir et Sombre utilisés par les applications. Les anciennes préférences récupèrent leur décor non-AR courant comme valeur de retour. L’hôte 2D ne propose pas cette commande et ne démarre pas OpenXR.

Le runtime crée une couche `XR_FB_passthrough` de reconstruction avec opacité 1 et contours désactivés. La composition soumet cette couche en premier, puis la projection transparente des mains et les panneaux. Le renderer ignore le ciel et la géométrie 3D quand le fond choisi est Passthrough ; ses pixels transparents exposent donc la vue réelle. Le mode de mélange OpenXR reste OPAQUE, conformément au runtime Quest. Le panneau UI garde son alpha, sans rééchantillonner une image de caméra.

L’activation démarre la fonctionnalité puis reprend la couche. Une erreur restaure le décor mémorisé et désactive la disponibilité jusqu’à une récupération du service ou une nouvelle session. L’activité en pause, la session STOPPING et le retour en 2D suspendent explicitement la couche puis la fonctionnalité. Les événements de panne, récupération et réinitialisation de passthrough sont traités et journalisés. L’état actif est séparé de la préférence sauvegardée, invalidant l’UI seulement lorsqu’il change. Les contrôles de flou et de luminosité du décor virtuel sont inactifs en AR ; leurs valeurs restent conservées.

Le passthrough est produit par le compositeur du casque, sans acquisition de pixels par l’application et sans permission CAMERA ajoutée. Les callbacks de rendu et les couches des applications conservent leur ordre et leurs politiques de profondeur. La vue réelle ne fournit pas ici de maillage de pièce ni d’occlusion automatique des panneaux par les objets physiques.
