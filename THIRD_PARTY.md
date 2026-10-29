# Composants et licences

Le code CyVR original est sous licence MIT (`LICENSE`). Les composants tiers conservent leurs licences.

Les logos conservés dans `assets/branding/` sont exclus de la licence MIT du projet et ne sont pas inclus dans le dépôt ni dans les archives de distribution.

| Composant | Version/source | Licence | Utilisation |
|---|---|---|---|
| OpenXR headers et loader Khronos | SDK 1.1.49, dépôt `KhronosGroup/OpenXR-SDK`, AAR Maven `org.khronos.openxr:openxr_loader_for_android:1.1.49` | Apache-2.0 ; certains fichiers proposent aussi MIT | API et chargement du runtime système |
| JsonCpp, embarqué dans le loader | Copie du SDK OpenXR 1.1.49 | MIT / domaine public selon le territoire | Lecture des manifestes du runtime |
| jnipp, embarqué dans le loader | Copie du SDK OpenXR 1.1.49 | MIT | Liaison JNI Android |
| android-jni-wrappers, embarqué dans le loader | Copie du SDK OpenXR 1.1.49, Copyright Collabora 2020–2021 | BSL-1.0 | Interfaces JNI générées |
| Vulkan Headers | tag `v1.3.290`, `KhronosGroup/Vulkan-Headers` | Apache-2.0 ou MIT selon le fichier ; option Apache retenue ici | Définitions de l’API ; pilote fourni par le système |
| Noto Sans Regular | `notofonts/noto-fonts`, tag `v20201206-phase3` | SIL OFL 1.1 | Police originale et atlas dérivé, sans modification du nom de la police |
| Android native_app_glue | Android NDK 27.2.12479018 | Apache-2.0 | Cycle de vie NativeActivity |
| LLVM libc++ / compiler runtime Android | Android NDK 27.2.12479018 | Apache-2.0 WITH LLVM-exception, selon les fichiers de la distribution | Bibliothèque standard C++ et support de compilation |

L’AAR OpenXR d’origine est vérifié par SHA-256 avant chaque paquet :
`9a731db8a21bfc5c4be47c8862a508a2e4db3d7167062dc65683cedf7563f0cd`.

Les avis complémentaires des dépendances du loader et du runtime C++ sont dans `third_party/notices` et inclus dans les distributions. Les fichiers Khronos vendoriés sont inchangés ; leur provenance est décrite dans `tools/fetch-deps.ps1` et leurs empreintes dans `third_party/SHA256SUMS`.

Les outils de compilation ont leurs propres licences : Clang/LLVM, CMake, Ninja, JDK, outils Android. Pillow est uniquement utilisé pour régénérer l’atlas et n’est pas intégré à l’application. Le système Quest, son runtime OpenXR et ses pilotes graphiques sont fournis par Meta et ne sont pas redistribués dans ce projet. Aucun SDK Meta propriétaire n’est embarqué.

Le gant Studio (`assets/hand_studio.bin` et son générateur `tools/bake_hands.py`) est une création originale du projet sous MIT. NumPy (BSD-3-Clause) sert uniquement à fabriquer ce fichier et n’est pas embarqué. Les icônes sont également originales, dessinées en géométrie par le code. Le maillage optionnel `XR_FB_hand_tracking_mesh` est reçu du runtime installé sur le casque et utilisé en mémoire ; aucun modèle Meta n’est copié dans les sources ni distribué avec l’APK.
