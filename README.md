###### Intro
*teleinfuse* est programme qui permet d'accéder aux informations délivrées par la sortie télé information cliente (TIC) des compteurs linky en mode standard.
Pour lire les données d'un compte en mode historique, voir le projet original : https://github.com/neomilium/teleinfuse

Grâce à [FUSE](http://fuse.sourceforge.net) et un montage simple, les informations délivrées par le compteur électrique sont accessibles via un système de fichier virtuel.

###### Pourquoi ?
La plupart des programmes utilisant le linky se basent sur les APIs Enedis puisque le linky est un compteur communiquant. Mais les données obtenues via Enedis sont du jour précédent. Impossible donc d'avoir des informations en live via cette méthode.
L'idée ici est d'utiliser la télé information client (TIC) du linky. La TIC existait bien avant le linky mais le linky introduit une TIC enrichie (appelée mode standard) qui permet d'obtenir de nombreuses informations.

Le programme est écrit en C pour consommer le moins de ressources possibles et pouvoir tourner sur un mini-pc (type Raspberrry PI ou moins puissant encore).
Des tests effectués avec des programmes écrits dans d'autres langages (dont Python) ont révélé une consommation de ressources très sensiblement supérieures (10% CPU en python sur un Raspberry Pi 3 contre des temps < 1% avec ce programme).

###### Comment ?

Via une aquisition de la TIC en utilisant un module USB.
Par exemple :
https://www.domotique-store.fr/domotique/modules-domotiques/detecteurs-capteurs-mesure/mesure-consommation-energetique/672-module-teleinfo-usb-cartelectronic-usbticlc-win.html

Il faut que le boitier supporte le mode TIC standard du linky (vitesse à 9600 bauds).

Puis en utilisant le programme teleinfuse pour obtenir un système de fichiers virtuels où chaque fichier représente une donnée TIC (liste ci-dessous)

Pour que teleinfuse se lance dès le boot, on peut utiliser une entrée fuse dans /etc/fstab
```
/usr/local/bin/teleinfuse#/dev/ttyUSB0 /mnt/teleinfo fuse user,allow_other,interval=2 0 0
```


###### Télé information cliente (TIC)

Données les plus intéressantes (pour toutes les données, voir page 18 du document présent dans 'doc') :
* EAST  : énergie active soutirée totale en Wh, en clair : ce que l'on a consommé au total depuis que l'on a ce compteur
* PREF  : puissance apparente de référence en kVA, en clair : ce que vous avez souscrit chez votre fournisseur (exemple: 9kVA, 12kVA)
* SINST : puissance apparente instantanée soutirée en VA, à comparer avec la souscription (voir donnée ci-dessus)
