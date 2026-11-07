# CyVR UI — notes de version

[Présentation du projet](README.fr.md) · [English](VERSION.md)

CyVR UI est **en développement** et distribué sous forme de versions de test expérimentales, signées pour le développement. Les sources et les paquets téléchargeables n’ont actuellement pas la même version.

| Composant | Version | Disponibilité |
| --- | --- | --- |
| Application de référence, bibliothèques partagées et outils Windows | 0.9.1 | Sources actuelles ; compilation avec les scripts du dépôt. L’éditeur ne dispose pas d’un paquet Windows publié séparément. |
| Application Quest de référence publiée | 0.8.0, tag de publication `v0.8.0-test.1` | [APK ARM64 hybride et strictement 2D](https://github.com/MrMybal/CyVR_UI/releases/tag/v0.8.0-test.1), avec empreintes de contrôle. |

## Sources actuelles : 0.9.1

- Éditeur Windows natif d’interfaces 2D : capture des écrans de référence, édition des propriétés, vues fidèle/schématique, annulation/rétablissement, JSON versionné et export JSON/Markdown/PNG.
- Bibliothèque optionnelle `cyvr_layout` pour charger et dessiner les documents modifiés en conservant les identifiants d’action et les zones interactives.
- Barre de fenêtre immersive fixe, avec icônes latérales au survol. Les prises sur toute la surface avec main ou manette déplacent la fenêtre gérée ; les bords et coins autorisés la redimensionnent en conservant les proportions du canevas.
- Déplacement avec orientation vers l’utilisateur et réglage de distance/rotation au joystick. Le contenu des applications compactes garde son entrée de prise ; une poignée explicitement exposée peut encore déplacer la fenêtre à la gâchette.
- Entrées applicatives résolues pour les manettes, défilement Android et points d’intégration étendus pour les hôtes et le rendu.

Ces ajouts sont plus récents que l’APK téléchargeable 0.8.0-test.1. Voir les guides de l’[éditeur](docs/INTERFACE_EDITOR.md), des [commandes de fenêtre](docs/WINDOW_INTERACTIONS.md) et des [contrats des hôtes Android](docs/ANDROID_HOSTS.md), en anglais.

## Version de test publiée : 0.8.0-test.1

L’APK publié s’ouvre dans une fenêtre Android 2D native et propose une immersion OpenXR optionnelle. Il comprend les contrôles remaniés, les thèmes, les traductions anglais/français, trois scènes PBR originales, deux fonds photographiques CC0, la vue réelle du casque optionnelle et la composition de profondeur mains/panneau lorsque disponible.

- [APK hybride](https://github.com/MrMybal/CyVR_UI/releases/download/v0.8.0-test.1/CyVR_UI-0.8.0-test.1-arm64.apk) : 2D native avec VR/AR immersive optionnelle.
- [APK strictement 2D](https://github.com/MrMybal/CyVR_UI/releases/download/v0.8.0-test.1/CyVR_UI-0.8.0-test.1-panel-arm64.apk) : sans chargeur OpenXR ni ressources d’environnements immersifs.
- [Empreintes de contrôle](https://github.com/MrMybal/CyVR_UI/releases/download/v0.8.0-test.1/SHA256SUMS.txt) · [Notes de publication](https://github.com/MrMybal/CyVR_UI/releases/tag/v0.8.0-test.1).

Les APK partagent l’identifiant `com.cyvr.ui` et s’installent comme variantes alternatives. Les transitions hybrides nécessitent Horizon OS v69 ou ultérieur.

## Limites documentées

- Les Quest 2, Quest 3S et Quest Pro sont des cibles déclarées ; leur validation complète reste à réaliser. Les observations sur Quest 3 ne démontrent pas la compatibilité avec tous les modèles.
- Les performances prolongées, le comportement thermique, le cycle de vie de la vue réelle, l’occlusion des mains par le compositeur et les nouveaux gestes de prise nécessitent encore des essais prolongés dans le casque.
- Le canevas logique de 1440 × 990 est mis à l’échelle en conservant ses proportions, avec des bandes si nécessaire ; sa disposition ne se réorganise pas automatiquement dans les fenêtres étroites.
- Le flou de fenêtre échantillonne l’environnement virtuel de l’application. Il ne floute pas les autres applications ni les images des caméras du casque. Les panoramas photographiques sont monoscopiques et n’offrent pas de parallaxe de la scène en translation.
- Les dispositions éditées nécessitent une intégration applicative ; la référence Quest ne charge pas automatiquement les JSON de l’éditeur.
- La rotation manuelle en tangage/roulis, le redimensionnement à deux mains, les panneaux courbes, les champs de texte éditables, le clavier virtuel, le suivi oculaire, la mise en forme complexe du texte et la disposition de droite à gauche ne sont pas implémentés. Le temps d’exécution GPU n’est pas instrumenté.

## Problèmes signalés

Les [notes de publication 0.8.0](https://github.com/MrMybal/CyVR_UI/releases/tag/v0.8.0-test.1) ne fournissent pas de liste détaillée des bogues. Consultez le [suivi des problèmes](https://github.com/MrMybal/CyVR_UI/issues) et indiquez la version, le modèle du casque et les étapes de reproduction pour signaler un problème. Les limites ci-dessus ne constituent pas une liste de tickets de bogues confirmés.
