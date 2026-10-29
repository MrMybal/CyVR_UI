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
   Android : NativeActivity → OpenXR → runtime du casque
   Windows : cible Vulkan hors écran → fenêtre Win32
```

## Fichiers

| Fichier | Rôle |
|---|---|
| `include/cyvr/core.hpp`, `src/core.cpp` | Types, rayons, panneaux, texte UTF-8, maillage UI, sélection/capture, modèle de référence, statistiques |
| `src/ui.cpp` | Pages et style du catalogue, contrôles et cibles interactives |
| `include/cyvr/spatial.hpp`, `src/spatial.cpp` | Matrices de vue/projection, géométrie des mains, articulations et lasers |
| `include/cyvr/gpu.hpp`, `src/gpu.cpp` | Ressources Vulkan, atlas, synchronisation, pipelines 2D/3D et captures PC |
| `src/xr_app.cpp` | Session OpenXR, entrées, capacités, swapchains, placement et composition |
| `src/android_main.cpp` | Point d’entrée NativeActivity, attachement JNI et traitement d’erreur |
| `src/desktop.cpp` | Inspecteur Windows natif utilisant le même cœur et le même backend Vulkan |
| `include/cyvr/storage.hpp` | Préférences et export JSON |
| `assets/fonts` | Police originale et licence OFL |
| `assets/font_atlas.bin`, `include/cyvr/font_data.hpp` | Atlas R8 et métriques, régénérables par `tools/bake_font.py` |

## Rendu

L’interface principale est une couche quadrilatère OpenXR, avec sa propre swapchain Vulkan sRGB. Le runtime assure la présentation stéréo du panneau. La texture est réutilisée tant que le contenu ne change pas. Le panneau diagnostic et les réticules utilisent d’autres couches.

Les mains et lasers utilisent des normales lissées, un éclairage simple et une profondeur D32. Deux vues OpenXR les projettent dans une couche qui dessine également le décor natif. Leur résolution est plafonnée à 1024 pixels sur le grand côté ; les textes restent dans leur couche de panneau indépendante. Les ressources GPU sont créées au démarrage.

Le décor procédural Studio/Nuit dépend de rayons en espace monde. Le verre dépoli évalue un filtre gaussien 3 × 3 sur ce décor dans une cible 480 × 330, puis l’échantillonne linéairement sous l’UI. Le contenu UI conserve sa géométrie tant que sa révision ne change pas ; le fond de verre s’actualise lors d’un déplacement du point de vue, au maximum 30 Hz hors modifications de contenu. Le texte est dessiné après le flou et reste opaque. Les surfaces suivent l’opacité réglée. Avec le passthrough, le matériau devient transparent teinté sans filtrer l’image du casque.

Ordre des couches : passthrough facultatif, projection décor/mains/rayons, fenêtre principale, HUD, barre flottante, réticules. La profondeur ne traverse pas les couches de composition : les mains apparaissent derrière les panneaux même si leur position physique est plus proche.

Le backend utilise une file graphique et un buffer de commande réutilisé avec une fence. Cette première version attend explicitement la fin des mises à jour avant de relâcher les images à OpenXR. Cela simplifie la propriété des ressources ; l’ajout de plusieurs images en vol demandera du profilage avant modification.

La référence Windows effectue une lecture GPU uniquement pour l’inspection sur écran plat. **Le chemin Quest ne fait pas cette lecture.**

## Entrées et placement

Les actions OpenXR sont séparées des contrôles visuels. Les manettes utilisent le profil Oculus Touch standard : aim/grip, gâchettes et haptique. Le suivi des mains est optionnel, vérifié par capacité ; il utilise `XR_EXT_hand_tracking`, avec `XR_FB_hand_tracking_aim` si disponible et un repli géométrique sinon.

Un rayon est transformé dans le repère du panneau pour obtenir des coordonnées UI. L’appui capture le contrôle ; le relâchement valide seulement la cible correspondante. Les curseurs conservent la capture lorsqu’on sort de leur rectangle. Les modales remplacent les cibles interactives de fond. Deux pointeurs sont pris en charge.

Les poignées supérieure et flottante capturent un pointeur et conservent la profondeur de saisie. Elles déplacent la fenêtre, le HUD et la barre, avec une distance bornée entre 0,8 et 3 mètres par rapport à la tête. La fenêtre se tourne vers l’utilisateur au relâchement. Les hit tests des deux surfaces sont comparés en profondeur ; la barre a ses propres coordonnées virtuelles sous le panneau principal.

Trois états de fenêtre : ouverte, réduite (page conservée), fermée (retour à l’accueil). La barre reste disponible pour restaurer ou rouvrir. Les hit regions du contenu sont retirées pendant les deux états masqués. Trois modes de placement : monde, orientation vers l’utilisateur sans translation, suivi amorti de la tête. La fenêtre regarde vers la tête avec une inclinaison bornée à 45°, sans roulis ; le suivi se fige pendant un appui et saisir une fenêtre suivie rétablit l’ancrage monde. Les thèmes, matériaux, modèles et préférences de suivi sont persistés localement.

Le panneau mesure par défaut 1,20 × 0,825 m à 1,35 m, pour une texture de 1440 × 990 pixels. Les boutons principaux utilisent des cibles d’environ 5 cm. Les petits textes secondaires et le confort doivent être évalués sur chaque casque.

## Extension

Une application peut fournir `UiModel::renderApplication(DrawList&)` pour remplacer le panneau complet et `handleApplicationAction(int)` pour traiter ses actions. Renvoyer `false` conserve le traitement du catalogue. `applicationWidget(...)` enregistre les cibles dans le système commun de pointeurs ; `clearApplicationWidgets()` les retire lors d’un nouveau dessin. Les identifiants métier doivent rester inférieurs à 600, réservés au dock spatial. `toast()` expose le retour d’état à un panneau personnalisé. Annuler les captures avec `navigate(page)` lors d’un changement de collection ou de page, puis détacher les callbacks avant de détruire leur contrôleur. CyVR_Launcher utilise ces points d’extension tout en réutilisant directement les moteurs du socle.

Le catalogue est un exemple, pas un gestionnaire universel de fenêtres. Ajouter une page revient à dessiner ses éléments, enregistrer leurs rectangles et traiter leurs identifiants d’action dans le modèle. Pour un produit plus vaste, extraire un routeur de pages, des composants métier et un gestionnaire de fenêtres au-dessus des bibliothèques existantes, sans déplacer la logique métier dans Vulkan ou OpenXR.

Les palettes Graphite, Clair et Océan sont définies par `palette(Settings)` dans `src/ui.cpp`. Les icônes sont dessinées par les primitives vectorielles du moteur. Aucun fichier web, fonte d’icônes ou image externe n’est utilisé.

## Modèles de mains

Le modèle Studio est original : `tools/bake_hands.py` construit une union implicite lissée, extraite par tétraèdres. Le fichier `assets/hand_studio.bin` utilise un en-tête de 12 octets (`CYRH`, uint32 version 1, uint32 nombre de sommets), puis des sommets position XYZ et normale XYZ en float32 little endian, triangles non indexés. Le repère de fabrication est +Y vers le dos de la main, +Z vers le poignet, −X vers le pouce. `controllerHand` effectue le miroir gauche/droite et la conversion vers le repère OpenXR grip, puis applique l’échelle et l’inclinaison. Le fichier actuel contient 17 484 triangles. Un `HandSurface` alternatif peut être injecté dans cet appel ou remplacer la ressource au build.

Pour les mains nues, le mode automatique obtient une seule fois le maillage `XR_FB_hand_tracking_mesh`, conservé en mémoire sans export. À chaque image, `skinnedHand` applique les poses de joints multipliées par l’inverse des poses de liaison et l’échelle mesurée `XrHandTrackingScaleFB`. `HandSkin` accepte aussi un modèle fourni par l’application : positions et normales, indices triangulaires, 26 poses de liaison et jusqu’à quatre influences normalisées par sommet. Les articulations utilisent l’ordre `XR_HAND_JOINT_SET_DEFAULT_EXT`. Les modèles importés doivent posséder leur propre licence de redistribution ; seul le gant original est inclus ici.

Les modes Studio et Squelette peuvent également dessiner les joints suivis sans utiliser le maillage du runtime. Masquées ne modifie ni le suivi ni la sélection. Les modèles des manettes restent une approximation de pose, pas un suivi des doigts.
