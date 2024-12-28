Tema 2 Protocoale de Comunicatie 2023-2024
Sandulache Mihnea-Stefan 322CC
Teste trecute pe local: toate

Timp de implementare: 40 de ore
--------------------------------------------------------------------------------
    Pentru implementarea server-ului am folosit o structura pentru mesajele
primite de la clientul UDP si trimise catre clientul TCP si o structura pentru
clientul TCP. Pentru multiplexarea IO-ului am folosit un set de file descriptori
si functia select. Dupa setarea socketilor si realizarea setup-ului necesar, am
tratat 4 cazuri in functie de file descriptori: primesc mesaje pe socket-ul de
listen UDP sau TCP, de la tastatura sau de la un client TCP deja conectat.Pentru
cazul de primire al mesajelor de la tastatura, avem doar comanda de exit, in urma
careia sunt oprite toate conexiunile si se inchide server-ul. Pentru mesajele
primite pe socket-ul de listen de TCP, am tratat cererile de conectare ale
clientilor noi si am considerat atat cazul in care clientul este complet nou,
cazul in care acesta a mai fost conectat si doar ii actualizam file descriptor-ul
si cazul in care se incearca conectarea unui client cu un id deja existent.
    Pentru socket-ul de UDP de listen, am primit mesajele de la clientii UDP, am
verificat daca s-a trimit un mesaj gol si am iterat prin lista de subscriberi si
si am trimis celor conectati mesajele corespunzatoare in functie de abonarile lor.
De asemenea, in cazul in care am trimis un mesaj pe un topic, ies din bucla de
trimitere pentru a evita cazul in care s-ar trimite doua mesaje daca inainte
clientul a fost abonat la un topic fara wildcard, iar ulterior clientul s-a abonat
la un topic cu wildcard care face match cu cel fara wildcard. Pe ultimul caz de
file descriptor, am trat cererile de subscribe si unsubscribe ale clientilor TCP
deja conectati, fiecare client avand in structura sa un vector de topic-uri si un
vector in care vom marca abonarea sau dezabonarea la topic-uri, acestea fiind
actualizate de fiecare data cand se efectueaza o astfel de cerere. Pentru cererea
de exit, am afisat un mesaj de deconectare.
    In client, am tratat doua cazuri pentru input de la tastatura sau pentru mesaje
primite de la server. Pentru input-ul de la tastatura, am trimis comanda, urmata de
topic, in cazul in care era vorba de subscribe si unsubscribe, catre server. Pentru
mesajele de la server, am preluat datele in structura de mesaj si am format numerele
dupa tipul mesajului, conform cerintei. 
    Pentru incadrarea mesajelor am folosit structuri si buffere si trimit intotdeauna
lungimea mesajului pentru a primi exact cat trimit.