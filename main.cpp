/*
 * ============================================================================
 *  Gestion d'une agence de voyage : passagers, vols, réservations, paiements
 *  Application console en C++17 (Programmation Orientée Objet)
 *
 *  Compilation :  g++ -std=c++17 -Wall -Wextra -pedantic -o agence main.cpp
 *  Exécution   :  ./agence            (ou  ./agence --demo  pour des données d'exemple)
 * ============================================================================
 *
 *  Règles de gestion appliquées (voir l'analyse) :
 *   R1  Un passager doit être enregistré avant de réserver.
 *   R2  Un vol ne peut pas recevoir plus de réservations que sa capacité.
 *   R3  Un passager ne peut pas être enregistré deux fois sur le même vol.
 *   R4  Une réservation est liée à un seul passager et à un seul vol.
 *   R5  Une réservation annulée ne peut plus être payée.
 *   R6  Une place n'est définitive qu'après validation de la réservation.
 *   R7  Une réservation payée puis annulée suit le statut de remboursement
 *       (AUCUN -> A_REMBOURSER -> REMBOURSE).
 */

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using std::cout;
using std::string;
using std::unique_ptr;
using std::vector;

// ============================================================================
//  Exceptions
// ============================================================================

/** Violation d'une règle de gestion (opération interdite). */
class ErreurMetier : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/** Fin du flux d'entrée (Ctrl+D / Ctrl+Z) : on quitte proprement. */
class FinDeSaisie : public std::runtime_error {
public:
    FinDeSaisie() : std::runtime_error("fin de la saisie") {}
};

// ============================================================================
//  Énumérations
// ============================================================================

enum class StatutReservation { EN_ATTENTE, VALIDEE, ANNULEE };
enum class StatutPaiement { EFFECTUE, ANNULE };
enum class StatutRemboursement { AUCUN, A_REMBOURSER, REMBOURSE };
enum class ModePaiement { ESPECES, MOBILE_MONEY, CARTE, VIREMENT };

string texte(StatutReservation s) {
    switch (s) {
        case StatutReservation::EN_ATTENTE: return "EN_ATTENTE";
        case StatutReservation::VALIDEE:    return "VALIDEE";
        case StatutReservation::ANNULEE:    return "ANNULEE";
    }
    return "?";
}

string texte(StatutPaiement s) {
    switch (s) {
        case StatutPaiement::EFFECTUE: return "EFFECTUE";
        case StatutPaiement::ANNULE:   return "ANNULE";
    }
    return "?";
}

string texte(StatutRemboursement s) {
    switch (s) {
        case StatutRemboursement::AUCUN:        return "AUCUN";
        case StatutRemboursement::A_REMBOURSER: return "A_REMBOURSER";
        case StatutRemboursement::REMBOURSE:    return "REMBOURSE";
    }
    return "?";
}

string texte(ModePaiement m) {
    switch (m) {
        case ModePaiement::ESPECES:      return "ESPECES";
        case ModePaiement::MOBILE_MONEY: return "MOBILE_MONEY";
        case ModePaiement::CARTE:        return "CARTE";
        case ModePaiement::VIREMENT:     return "VIREMENT";
    }
    return "?";
}

// ============================================================================
//  Utilitaires : validation des données
// ============================================================================

namespace Validation {

string trim(const string& s) {
    const char* blancs = " \t\r\n";
    const auto debut = s.find_first_not_of(blancs);
    if (debut == string::npos) return "";
    const auto fin = s.find_last_not_of(blancs);
    return s.substr(debut, fin - debut + 1);
}

string enMinuscules(string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool contient(const string& texteComplet, const string& motif) {
    return enMinuscules(texteComplet).find(enMinuscules(motif)) != string::npos;
}

bool nonVide(const string& s) { return !s.empty(); }

/** Lettres (accents UTF-8 acceptés), espaces, tirets et apostrophes. */
bool nomValide(const string& s) {
    bool uneLettre = false;
    for (unsigned char c : s) {
        if (std::isalpha(c) || c >= 128) uneLettre = true;
        else if (c != ' ' && c != '-' && c != '\'') return false;
    }
    return uneLettre;
}

bool telephoneValide(const string& s) {
    size_t chiffres = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (std::isdigit(c)) ++chiffres;
        else if (c == ' ' || (c == '+' && i == 0)) continue;
        else return false;
    }
    return chiffres >= 8 && chiffres <= 15;
}

bool emailValide(const string& s) {
    if (s.find(' ') != string::npos) return false;
    const auto arobase = s.find('@');
    if (arobase == string::npos || arobase == 0) return false;
    if (s.find('@', arobase + 1) != string::npos) return false;
    const auto point = s.find('.', arobase + 2);
    return point != string::npos && point + 1 < s.size();
}

bool pieceValide(const string& s) {
    if (s.size() < 5 || s.size() > 20) return false;
    return std::all_of(s.begin(), s.end(),
                       [](unsigned char c) { return std::isalnum(c) != 0; });
}

bool numeroVolValide(const string& s) {
    if (s.size() < 3 || s.size() > 10) return false;
    return std::all_of(s.begin(), s.end(),
                       [](unsigned char c) { return std::isalnum(c) != 0 || c == '-'; });
}

bool chiffres(const string& s, size_t debut, size_t longueur) {
    for (size_t i = debut; i < debut + longueur; ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

/** Format JJ/MM/AAAA avec contrôle du calendrier (années bissextiles incluses). */
bool dateValide(const string& s) {
    if (s.size() != 10 || s[2] != '/' || s[5] != '/') return false;
    if (!chiffres(s, 0, 2) || !chiffres(s, 3, 2) || !chiffres(s, 6, 4)) return false;
    const int jour = std::stoi(s.substr(0, 2));
    const int mois = std::stoi(s.substr(3, 2));
    const int annee = std::stoi(s.substr(6, 4));
    if (annee < 2000 || annee > 2100 || mois < 1 || mois > 12 || jour < 1) return false;
    static const int joursParMois[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool bissextile = (annee % 4 == 0 && annee % 100 != 0) || annee % 400 == 0;
    const int maxJours = joursParMois[mois - 1] + ((mois == 2 && bissextile) ? 1 : 0);
    return jour <= maxJours;
}

/** Format HH:MM. */
bool heureValide(const string& s) {
    if (s.size() != 5 || s[2] != ':' || !chiffres(s, 0, 2) || !chiffres(s, 3, 2)) return false;
    return std::stoi(s.substr(0, 2)) < 24 && std::stoi(s.substr(3, 2)) < 60;
}

}  // namespace Validation

// ============================================================================
//  Utilitaires : saisie sécurisée au clavier
// ============================================================================

namespace Saisie {

string lireLigne(const string& invite) {
    cout << invite << std::flush;
    string ligne;
    if (!std::getline(std::cin, ligne)) throw FinDeSaisie();
    return Validation::trim(ligne);
}

/** Redemande la saisie tant que le validateur la refuse. */
string lireValide(const string& invite, bool (*valide)(const string&), const string& aide) {
    while (true) {
        const string s = lireLigne(invite);
        if (valide(s)) return s;
        cout << "  Saisie invalide : " << aide << "\n";
    }
}

int lireEntier(const string& invite, int min, int max) {
    while (true) {
        const string s = lireLigne(invite);
        try {
            size_t pos = 0;
            const int v = std::stoi(s, &pos);
            if (pos == s.size() && v >= min && v <= max) return v;
        } catch (const std::exception&) {
            // tombe dans le message d'erreur ci-dessous
        }
        cout << "  Saisie invalide : entrez un nombre entre " << min << " et " << max << ".\n";
    }
}

double lireReel(const string& invite, double min) {
    while (true) {
        const string s = lireLigne(invite);
        try {
            size_t pos = 0;
            const double v = std::stod(s, &pos);
            if (pos == s.size() && std::isfinite(v) && v >= min) return v;
        } catch (const std::exception&) {
        }
        cout << "  Saisie invalide : entrez un nombre supérieur ou égal à " << min << ".\n";
    }
}

bool lireOuiNon(const string& invite) {
    while (true) {
        const string s = Validation::enMinuscules(lireLigne(invite + " (o/n) : "));
        if (s == "o" || s == "oui") return true;
        if (s == "n" || s == "non") return false;
        cout << "  Saisie invalide : répondez par o (oui) ou n (non).\n";
    }
}

}  // namespace Saisie

// ============================================================================
//  Utilitaires : affichage
// ============================================================================

string dateDuJour() {
    const std::time_t t = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char tampon[16];
    std::strftime(tampon, sizeof tampon, "%d/%m/%Y", &local);
    return tampon;
}

string formaterMontant(double montant) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(0) << montant << " FCFA";
    return os.str();
}

// ============================================================================
//  Classe Passager
// ============================================================================

class Passager {
public:
    Passager(int id, const string& nom, const string& prenom, const string& telephone,
             const string& email, const string& numeroPiece)
        : idPassager_(id), nom_(Validation::trim(nom)), prenom_(Validation::trim(prenom)),
          telephone_(Validation::trim(telephone)), email_(Validation::trim(email)),
          numeroPiece_(Validation::trim(numeroPiece)) {
        if (id <= 0) throw std::invalid_argument("Identifiant de passager invalide.");
        if (!Validation::nomValide(nom_)) throw std::invalid_argument("Nom invalide.");
        if (!Validation::nomValide(prenom_)) throw std::invalid_argument("Prénom invalide.");
        if (!Validation::telephoneValide(telephone_))
            throw std::invalid_argument("Numéro de téléphone invalide.");
        if (!Validation::emailValide(email_)) throw std::invalid_argument("Email invalide.");
        if (!Validation::pieceValide(numeroPiece_))
            throw std::invalid_argument("Numéro de pièce d'identité invalide.");
    }

    int getIdPassager() const { return idPassager_; }

    void afficher() const {
        cout << "    Passager n°" << idPassager_ << " : " << prenom_ << " " << nom_ << "\n"
             << "      Téléphone : " << telephone_ << "\n"
             << "      Email     : " << email_ << "\n"
             << "      Pièce ID  : " << numeroPiece_ << "\n";
    }

private:
    int idPassager_;
    string nom_;
    string prenom_;
    string telephone_;
    string email_;
    string numeroPiece_;
};

// ============================================================================
//  Classe Vol
// ============================================================================

class Vol {
public:
    Vol(const string& numero, const string& depart, const string& arrivee, const string& date,
        const string& heure, int capacite, double prix)
        : numeroVol_(Validation::trim(numero)), villeDepart_(Validation::trim(depart)),
          villeArrivee_(Validation::trim(arrivee)), dateDepart_(Validation::trim(date)),
          heureDepart_(Validation::trim(heure)), capacite_(capacite), placesReservees_(0),
          prix_(prix) {
        if (!Validation::numeroVolValide(numeroVol_)) throw std::invalid_argument("Numéro de vol invalide.");
        if (!Validation::nomValide(villeDepart_)) throw std::invalid_argument("Ville de départ invalide.");
        if (!Validation::nomValide(villeArrivee_)) throw std::invalid_argument("Ville d'arrivée invalide.");
        if (Validation::enMinuscules(villeDepart_) == Validation::enMinuscules(villeArrivee_))
            throw std::invalid_argument("Les villes de départ et d'arrivée doivent être différentes.");
        if (!Validation::dateValide(dateDepart_)) throw std::invalid_argument("Date invalide (JJ/MM/AAAA).");
        if (!Validation::heureValide(heureDepart_)) throw std::invalid_argument("Heure invalide (HH:MM).");
        if (capacite_ <= 0) throw std::invalid_argument("La capacité doit être positive.");
        if (prix_ < 0) throw std::invalid_argument("Le prix ne peut pas être négatif.");
    }

    // --- Accesseurs ---
    const string& getNumeroVol() const { return numeroVol_; }
    const string& getVilleDepart() const { return villeDepart_; }
    const string& getVilleArrivee() const { return villeArrivee_; }
    const string& getDateDepart() const { return dateDepart_; }
    const string& getHeureDepart() const { return heureDepart_; }
    int getCapacite() const { return capacite_; }
    int getPlacesReservees() const { return placesReservees_; }
    double getPrix() const { return prix_; }

    // --- Mutateurs (avec contrôle) ---
    void setPrix(double prix) {
        if (prix < 0) throw std::invalid_argument("Le prix ne peut pas être négatif.");
        prix_ = prix;
    }
    void setDateDepart(const string& date) {
        if (!Validation::dateValide(date)) throw std::invalid_argument("Date invalide (JJ/MM/AAAA).");
        dateDepart_ = date;
    }
    void setHeureDepart(const string& heure) {
        if (!Validation::heureValide(heure)) throw std::invalid_argument("Heure invalide (HH:MM).");
        heureDepart_ = heure;
    }

    // --- Comportement ---
    int getPlacesDisponibles() const { return capacite_ - placesReservees_; }
    bool estDisponible() const { return getPlacesDisponibles() > 0; }

    /** R2 : refuse la réservation si le vol est complet. */
    void reserverPlace() {
        if (!estDisponible())
            throw ErreurMetier("Le vol " + numeroVol_ + " est complet (capacité : " +
                               std::to_string(capacite_) + ").");
        ++placesReservees_;
    }

    void libererPlace() {
        if (placesReservees_ <= 0) throw std::logic_error("Aucune place à libérer sur ce vol.");
        --placesReservees_;
    }

    static void afficherEnTete() {
        cout << "  " << std::left << std::setw(9) << "Vol" << std::setw(14) << "Depart"
             << std::setw(14) << "Arrivee" << std::setw(12) << "Date" << std::setw(7) << "Heure"
             << std::setw(9) << "Places" << "Prix\n"
             << "  " << string(78, '-') << "\n";
    }

    void afficher() const {
        const string places = std::to_string(getPlacesDisponibles()) + "/" + std::to_string(capacite_);
        cout << "  " << std::left << std::setw(9) << numeroVol_ << std::setw(14) << villeDepart_
             << std::setw(14) << villeArrivee_ << std::setw(12) << dateDepart_ << std::setw(7)
             << heureDepart_ << std::setw(9) << places << formaterMontant(prix_) << "\n";
    }

private:
    string numeroVol_;
    string villeDepart_;
    string villeArrivee_;
    string dateDepart_;
    string heureDepart_;
    int capacite_;
    int placesReservees_;
    double prix_;
};

// ============================================================================
//  Classe Reservation (association : 1 Passager, 1 Vol, 0..1 Paiement)
// ============================================================================

class Paiement;  // déclaration anticipée

class Reservation {
public:
    Reservation(int id, Passager* passager, Vol* vol, int numeroPlace, const string& date)
        : idReservation_(id), dateReservation_(date), numeroPlace_(numeroPlace),
          statut_(StatutReservation::EN_ATTENTE), passager_(passager), vol_(vol), paiement_(nullptr) {
        if (id <= 0) throw std::invalid_argument("Identifiant de réservation invalide.");
        if (passager_ == nullptr || vol_ == nullptr)
            throw std::invalid_argument("Une réservation exige un passager et un vol (R4).");
        if (numeroPlace_ <= 0) throw std::invalid_argument("Numéro de place invalide.");
    }

    int getIdReservation() const { return idReservation_; }
    const string& getDateReservation() const { return dateReservation_; }
    int getNumeroPlace() const { return numeroPlace_; }
    StatutReservation getStatut() const { return statut_; }
    Passager* getPassager() const { return passager_; }
    Vol* getVol() const { return vol_; }
    Paiement* getPaiement() const { return paiement_; }

    bool estValidee() const { return statut_ == StatutReservation::VALIDEE; }
    bool estAnnulee() const { return statut_ == StatutReservation::ANNULEE; }

    /** R6 : la place devient définitive. */
    void valider() {
        if (estAnnulee()) throw ErreurMetier("Impossible de valider une réservation annulée.");
        if (estValidee()) throw ErreurMetier("La réservation est déjà validée.");
        statut_ = StatutReservation::VALIDEE;
    }

    /** Annule la réservation et libère la place sur le vol. */
    void annuler() {
        if (estAnnulee()) throw ErreurMetier("La réservation est déjà annulée.");
        statut_ = StatutReservation::ANNULEE;
        vol_->libererPlace();
    }

    bool peutEtrePayee() const;  // R5 (défini après Paiement)

    void lierPaiement(Paiement* paiement) { paiement_ = paiement; }

    static void afficherEnTete() {
        cout << "  " << std::left << std::setw(6) << "No" << std::setw(12) << "Date"
             << std::setw(10) << "Passager" << std::setw(9) << "Vol" << std::setw(7) << "Place"
             << std::setw(12) << "Statut" << "Paiement\n"
             << "  " << string(72, '-') << "\n";
    }

    void afficherLigne() const;
    void afficher() const;

private:
    int idReservation_;
    string dateReservation_;
    int numeroPlace_;
    StatutReservation statut_;
    Passager* passager_;  // association (R4)
    Vol* vol_;            // association (R4)
    Paiement* paiement_;  // association 0..1
};

// ============================================================================
//  Classe Paiement
// ============================================================================

class Paiement {
public:
    Paiement(int id, Reservation* reservation, double montant, ModePaiement mode)
        : idPaiement_(id), montant_(montant), datePaiement_(dateDuJour()), mode_(mode),
          statut_(StatutPaiement::EFFECTUE), statutRemboursement_(StatutRemboursement::AUCUN),
          reservation_(reservation) {
        if (id <= 0) throw std::invalid_argument("Identifiant de paiement invalide.");
        if (reservation_ == nullptr) throw std::invalid_argument("Un paiement exige une réservation.");
        if (montant_ < 0) throw std::invalid_argument("Le montant ne peut pas être négatif.");
    }

    int getIdPaiement() const { return idPaiement_; }
    double getMontant() const { return montant_; }
    const string& getDatePaiement() const { return datePaiement_; }
    ModePaiement getMode() const { return mode_; }
    StatutPaiement getStatut() const { return statut_; }
    StatutRemboursement getStatutRemboursement() const { return statutRemboursement_; }
    Reservation* getReservation() const { return reservation_; }

    /**
     * Enregistre le paiement : contrôle R5, valide la réservation (R6) et la lie à ce paiement.
     * L'agence n'ajoute le paiement à sa collection que si cette méthode réussit.
     */
    void enregistrer() {
        if (reservation_->estAnnulee())
            throw ErreurMetier("Une réservation annulée ne peut plus être payée (R5).");
        if (!reservation_->peutEtrePayee()) throw ErreurMetier("Cette réservation est déjà payée.");
        statut_ = StatutPaiement::EFFECTUE;
        statutRemboursement_ = StatutRemboursement::AUCUN;
        if (!reservation_->estValidee()) reservation_->valider();
        reservation_->lierPaiement(this);
    }

    /** Annule le paiement : l'argent reçu doit être remboursé (R7). */
    void annuler() {
        if (statut_ == StatutPaiement::ANNULE) throw ErreurMetier("Ce paiement est déjà annulé.");
        statut_ = StatutPaiement::ANNULE;
        statutRemboursement_ = StatutRemboursement::A_REMBOURSER;
    }

    /** A_REMBOURSER -> REMBOURSE. */
    void traiterRemboursement() {
        if (statut_ != StatutPaiement::ANNULE ||
            statutRemboursement_ != StatutRemboursement::A_REMBOURSER)
            throw ErreurMetier("Aucun remboursement en attente pour ce paiement.");
        statutRemboursement_ = StatutRemboursement::REMBOURSE;
    }

    static void afficherEnTete() {
        cout << "  " << std::left << std::setw(6) << "No" << std::setw(12) << "Date"
             << std::setw(16) << "Montant" << std::setw(15) << "Mode" << std::setw(11) << "Statut"
             << "Remboursement\n"
             << "  " << string(75, '-') << "\n";
    }

    void afficher() const {
        cout << "  " << std::left << std::setw(6) << idPaiement_ << std::setw(12) << datePaiement_
             << std::setw(16) << formaterMontant(montant_) << std::setw(15) << texte(mode_)
             << std::setw(11) << texte(statut_) << texte(statutRemboursement_) << "\n";
    }

private:
    int idPaiement_;
    double montant_;
    string datePaiement_;
    ModePaiement mode_;
    StatutPaiement statut_;
    StatutRemboursement statutRemboursement_;
    Reservation* reservation_;  // association 1
};

// --- Méthodes de Reservation qui ont besoin de la définition complète de Paiement ---

bool Reservation::peutEtrePayee() const {
    if (estAnnulee()) return false;
    return paiement_ == nullptr || paiement_->getStatut() == StatutPaiement::ANNULE;
}

void Reservation::afficherLigne() const {
    string etatPaiement = "Non payee";
    if (paiement_ != nullptr) {
        etatPaiement = (paiement_->getStatut() == StatutPaiement::EFFECTUE)
                           ? "Payee"
                           : "Annule (" + texte(paiement_->getStatutRemboursement()) + ")";
    }
    cout << "  " << std::left << std::setw(6) << idReservation_ << std::setw(12) << dateReservation_
         << std::setw(10) << passager_->getIdPassager() << std::setw(9) << vol_->getNumeroVol()
         << std::setw(7) << numeroPlace_ << std::setw(12) << texte(statut_) << etatPaiement << "\n";
}

void Reservation::afficher() const {
    cout << "  ===== Réservation n°" << idReservation_ << " =====\n"
         << "    Date     : " << dateReservation_ << "\n"
         << "    Statut   : " << texte(statut_) << "\n"
         << "    Place    : " << numeroPlace_ << "\n";
    passager_->afficher();
    cout << "    Vol :\n";
    Vol::afficherEnTete();
    vol_->afficher();
    cout << "    Paiement :\n";
    if (paiement_ == nullptr) {
        cout << "      Aucun paiement enregistré.\n";
    } else {
        Paiement::afficherEnTete();
        paiement_->afficher();
    }
}

// ============================================================================
//  Classe Agence : gère les collections et le menu
// ============================================================================

class Agence {
public:
    Agence(const string& nom, const string& ville) : nom_(nom), ville_(ville) {}

    // ----- Opérations « métier » (réutilisables et testables sans clavier) -----

    Passager* ajouterPassager(const string& nom, const string& prenom, const string& tel,
                              const string& email, const string& piece) {
        auto p = std::make_unique<Passager>(prochainIdPassager_, nom, prenom, tel, email, piece);
        ++prochainIdPassager_;
        passagers_.push_back(std::move(p));
        return passagers_.back().get();
    }

    Vol* ajouterVol(const string& numero, const string& depart, const string& arrivee,
                    const string& date, const string& heure, int capacite, double prix) {
        if (trouverVol(numero) != nullptr)
            throw ErreurMetier("Un vol portant le numéro " + numero + " existe déjà.");
        vols_.push_back(std::make_unique<Vol>(numero, depart, arrivee, date, heure, capacite, prix));
        return vols_.back().get();
    }

    Passager* trouverPassager(int id) const {
        for (const auto& p : passagers_)
            if (p->getIdPassager() == id) return p.get();
        return nullptr;
    }

    Vol* trouverVol(const string& numero) const {
        const string cible = Validation::enMinuscules(Validation::trim(numero));
        for (const auto& v : vols_)
            if (Validation::enMinuscules(v->getNumeroVol()) == cible) return v.get();
        return nullptr;
    }

    Reservation* trouverReservation(int id) const {
        for (const auto& r : reservations_)
            if (r->getIdReservation() == id) return r.get();
        return nullptr;
    }

    Paiement* trouverPaiement(int id) const {
        for (const auto& p : paiements_)
            if (p->getIdPaiement() == id) return p.get();
        return nullptr;
    }

    /** Crée une réservation EN_ATTENTE en appliquant R1, R2 et R3. */
    Reservation* creerReservation(int idPassager, const string& numeroVol) {
        Passager* passager = trouverPassager(idPassager);
        if (passager == nullptr)
            throw ErreurMetier("Passager n°" + std::to_string(idPassager) +
                               " non enregistré : enregistrez-le avant de réserver (R1).");
        Vol* vol = trouverVol(numeroVol);
        if (vol == nullptr) throw ErreurMetier("Vol introuvable : " + numeroVol + ".");

        for (const auto& r : reservations_) {  // R3
            if (!r->estAnnulee() && r->getPassager() == passager && r->getVol() == vol)
                throw ErreurMetier("Ce passager a déjà une réservation sur le vol " +
                                   vol->getNumeroVol() + " (R3).");
        }
        if (!vol->estDisponible())  // R2
            throw ErreurMetier("Le vol " + vol->getNumeroVol() + " est complet (R2).");

        auto r = std::make_unique<Reservation>(prochainIdReservation_, passager, vol,
                                               premierePlaceLibre(*vol), dateDuJour());
        vol->reserverPlace();
        ++prochainIdReservation_;
        reservations_.push_back(std::move(r));
        return reservations_.back().get();
    }

    /** Crée un paiement pour une réservation (R5, R6). Le montant est le prix du vol. */
    Paiement* creerPaiement(int idReservation, ModePaiement mode) {
        Reservation* reservation = trouverReservation(idReservation);
        if (reservation == nullptr)
            throw ErreurMetier("Réservation n°" + std::to_string(idReservation) + " introuvable.");
        auto p = std::make_unique<Paiement>(prochainIdPaiement_, reservation,
                                            reservation->getVol()->getPrix(), mode);
        p->enregistrer();  // lève une exception si R5 est violée
        ++prochainIdPaiement_;
        paiements_.push_back(std::move(p));
        return paiements_.back().get();
    }

    /** Données d'exemple pour tester rapidement l'application. */
    void chargerDonneesDemo() {
        ajouterPassager("Dossou", "Koffi", "+229 97000001", "koffi.dossou@mail.com", "BJ123456");
        ajouterPassager("Adjovi", "Sènami", "+229 96000002", "senami.adjovi@mail.com", "BJ654321");
        ajouterPassager("Hounkpatin", "Eric", "+229 95000003", "eric.h@mail.com", "BJ112233");
        ajouterVol("AF-701", "Cotonou", "Paris", "20/12/2026", "23:30", 3, 450000);
        ajouterVol("ET-512", "Cotonou", "Addis-Abeba", "18/12/2026", "08:15", 2, 320000);
        ajouterVol("KQ-330", "Cotonou", "Lagos", "15/12/2026", "14:00", 2, 85000);
    }

    // ----- Opérations du menu (interaction avec l'utilisateur) -----

    void afficherMenu() const {
        cout << "\n================ MENU - " << nom_ << " (" << ville_ << ") ================\n"
             << "  1. Enregistrer un passager\n"
             << "  2. Ajouter un vol\n"
             << "  3. Afficher les vols disponibles\n"
             << "  4. Rechercher un vol\n"
             << "  5. Effectuer une réservation\n"
             << "  6. Enregistrer un paiement\n"
             << "  7. Annuler un paiement / une réservation\n"
             << "  8. Consulter des réservations\n"
             << "  9. Afficher les informations d'une réservation\n"
             << " 10. Quitter\n"
             << "====================================================================\n";
    }

    void enregistrerPassager() {
        cout << "--- Enregistrement d'un passager ---\n";
        const string nom = Saisie::lireValide("Nom : ", Validation::nomValide,
                                              "lettres, espaces, tirets ou apostrophes uniquement.");
        const string prenom = Saisie::lireValide("Prénom : ", Validation::nomValide,
                                                 "lettres, espaces, tirets ou apostrophes uniquement.");
        const string tel = Saisie::lireValide("Téléphone : ", Validation::telephoneValide,
                                              "8 à 15 chiffres (le + initial est permis).");
        const string email = Saisie::lireValide("Email : ", Validation::emailValide,
                                                "format attendu : nom@domaine.ext");
        const string piece = Saisie::lireValide("N° pièce d'identité : ", Validation::pieceValide,
                                                "5 à 20 lettres ou chiffres.");
        const Passager* p = ajouterPassager(nom, prenom, tel, email, piece);
        cout << "Passager enregistré. Identifiant attribué : " << p->getIdPassager()
             << " (à conserver pour réserver).\n";
    }

    void ajouterVol() {
        cout << "--- Ajout d'un vol ---\n";
        const string numero = Saisie::lireValide("Numéro de vol (ex. AF-701) : ", Validation::numeroVolValide,
                                                 "3 à 10 caractères : lettres, chiffres ou tiret.");
        if (trouverVol(numero) != nullptr)
            throw ErreurMetier("Un vol portant le numéro " + numero + " existe déjà.");
        string depart = Saisie::lireValide("Ville de départ : ", Validation::nomValide, "nom de ville invalide.");
        string arrivee;
        while (true) {
            arrivee = Saisie::lireValide("Ville d'arrivée : ", Validation::nomValide, "nom de ville invalide.");
            if (Validation::enMinuscules(arrivee) != Validation::enMinuscules(depart)) break;
            cout << "  Saisie invalide : l'arrivée doit différer du départ.\n";
        }
        const string date = Saisie::lireValide("Date de départ (JJ/MM/AAAA) : ", Validation::dateValide,
                                               "date inexistante ou mauvais format.");
        const string heure = Saisie::lireValide("Heure de départ (HH:MM) : ", Validation::heureValide,
                                                "format attendu HH:MM.");
        const int capacite = Saisie::lireEntier("Capacité (nombre de places) : ", 1, 1000);
        const double prix = Saisie::lireReel("Prix du billet (FCFA) : ", 0);
        const Vol* v = ajouterVol(numero, depart, arrivee, date, heure, capacite, prix);
        cout << "Vol " << v->getNumeroVol() << " ajouté.\n";
    }

    void afficherVolsDisponibles() const {
        cout << "--- Vols disponibles ---\n";
        bool trouve = false;
        for (const auto& v : vols_) {
            if (!v->estDisponible()) continue;
            if (!trouve) Vol::afficherEnTete();
            v->afficher();
            trouve = true;
        }
        if (!trouve) cout << "  Aucun vol disponible pour le moment.\n";
    }

    /** Recherche par numéro ou par villes. Retourne le vol si le résultat est unique. */
    Vol* rechercherVol() const {
        cout << "--- Recherche d'un vol ---\n"
             << "  1. Par numéro de vol\n  2. Par ville de départ / d'arrivée\n";
        const int choix = Saisie::lireEntier("Votre choix : ", 1, 2);
        vector<Vol*> resultats;
        if (choix == 1) {
            Vol* v = trouverVol(Saisie::lireValide("Numéro de vol : ", Validation::nonVide, "saisie vide."));
            if (v != nullptr) resultats.push_back(v);
        } else {
            const string depart = Saisie::lireLigne("Ville de départ (vide = toutes) : ");
            const string arrivee = Saisie::lireLigne("Ville d'arrivée (vide = toutes) : ");
            if (depart.empty() && arrivee.empty())
                throw ErreurMetier("Indiquez au moins une ville pour la recherche.");
            for (const auto& v : vols_) {
                const bool okDepart = depart.empty() || Validation::contient(v->getVilleDepart(), depart);
                const bool okArrivee = arrivee.empty() || Validation::contient(v->getVilleArrivee(), arrivee);
                if (okDepart && okArrivee) resultats.push_back(v.get());
            }
        }
        if (resultats.empty()) {
            cout << "  Aucun vol ne correspond à la recherche.\n";
            return nullptr;
        }
        Vol::afficherEnTete();
        for (const Vol* v : resultats) v->afficher();
        return resultats.size() == 1 ? resultats.front() : nullptr;
    }

    void effectuerReservation() {
        cout << "--- Nouvelle réservation ---\n";
        if (passagers_.empty())
            throw ErreurMetier("Aucun passager enregistré : enregistrez d'abord un passager (R1).");
        if (vols_.empty()) throw ErreurMetier("Aucun vol programmé : ajoutez d'abord un vol.");

        cout << "Passagers enregistrés : identifiants 1 à " << passagers_.size() << ".\n";
        const int idPassager = Saisie::lireEntier("Identifiant du passager : ", 1, 1000000);
        afficherVolsDisponibles();
        const string numeroVol = Saisie::lireValide("Numéro du vol à réserver : ", Validation::nonVide, "saisie vide.");

        Reservation* r = creerReservation(idPassager, numeroVol);
        cout << "Réservation n°" << r->getIdReservation() << " créée (place " << r->getNumeroPlace()
             << ", statut EN_ATTENTE).\n";
        if (Saisie::lireOuiNon("Valider la réservation maintenant")) {
            r->valider();
            cout << "Réservation validée : la place est définitivement réservée (R6).\n";
        } else {
            cout << "La réservation reste en attente (elle sera validée au paiement).\n";
        }
    }

    void enregistrerPaiement() {
        cout << "--- Enregistrement d'un paiement ---\n";
        const int idReservation = Saisie::lireEntier("N° de la réservation : ", 1, 1000000);
        const Reservation* r = trouverReservation(idReservation);
        if (r == nullptr) throw ErreurMetier("Réservation introuvable.");
        if (!r->peutEtrePayee()) {
            throw ErreurMetier(r->estAnnulee() ? "Une réservation annulée ne peut plus être payée (R5)."
                                               : "Cette réservation est déjà payée.");
        }
        cout << "Montant à payer : " << formaterMontant(r->getVol()->getPrix()) << "\n"
             << "  1. Espèces\n  2. Mobile money\n  3. Carte\n  4. Virement\n";
        const int m = Saisie::lireEntier("Mode de paiement : ", 1, 4);
        static const ModePaiement modes[] = {ModePaiement::ESPECES, ModePaiement::MOBILE_MONEY,
                                             ModePaiement::CARTE, ModePaiement::VIREMENT};
        const Paiement* p = creerPaiement(idReservation, modes[m - 1]);
        cout << "Paiement n°" << p->getIdPaiement() << " enregistré. La réservation est VALIDEE.\n";
    }

    void annulerPaiement() {
        cout << "--- Annulation d'un paiement ---\n";
        const int id = Saisie::lireEntier("N° du paiement : ", 1, 1000000);
        Paiement* p = trouverPaiement(id);
        if (p == nullptr) throw ErreurMetier("Paiement introuvable.");
        p->annuler();
        cout << "Paiement annulé. Remboursement : " << texte(p->getStatutRemboursement()) << ".\n"
             << "La réservation reste valide mais n'est plus payée.\n";
        proposerRemboursement(*p);
    }

    void annulerReservation() {
        cout << "--- Annulation d'une réservation ---\n";
        const int id = Saisie::lireEntier("N° de la réservation : ", 1, 1000000);
        Reservation* r = trouverReservation(id);
        if (r == nullptr) throw ErreurMetier("Réservation introuvable.");
        Paiement* p = r->getPaiement();
        const bool payee = (p != nullptr && p->getStatut() == StatutPaiement::EFFECTUE);
        r->annuler();
        cout << "Réservation annulée, la place est libérée sur le vol.\n";
        if (payee) {  // R7
            p->annuler();
            cout << "Réservation payée : le paiement est annulé, remboursement : "
                 << texte(p->getStatutRemboursement()) << ".\n";
            proposerRemboursement(*p);
        }
    }

    void consulterReservations() const {
        cout << "--- Consultation des réservations ---\n"
             << "  1. Toutes\n  2. D'un passager\n  3. D'un vol\n";
        const int choix = Saisie::lireEntier("Votre choix : ", 1, 3);
        int idPassager = 0;
        string numeroVol;
        if (choix == 2) {
            idPassager = Saisie::lireEntier("Identifiant du passager : ", 1, 1000000);
            if (trouverPassager(idPassager) == nullptr) throw ErreurMetier("Passager introuvable.");
        } else if (choix == 3) {
            numeroVol = Saisie::lireValide("Numéro de vol : ", Validation::nonVide, "saisie vide.");
            if (trouverVol(numeroVol) == nullptr) throw ErreurMetier("Vol introuvable.");
        }
        bool trouve = false;
        for (const auto& r : reservations_) {
            if (choix == 2 && r->getPassager()->getIdPassager() != idPassager) continue;
            if (choix == 3 && r->getVol() != trouverVol(numeroVol)) continue;
            if (!trouve) Reservation::afficherEnTete();
            r->afficherLigne();
            trouve = true;
        }
        if (!trouve) cout << "  Aucune réservation trouvée.\n";
    }

    void afficherInfosReservation() const {
        cout << "--- Informations d'une réservation ---\n";
        const int id = Saisie::lireEntier("N° de la réservation : ", 1, 1000000);
        const Reservation* r = trouverReservation(id);
        if (r == nullptr) throw ErreurMetier("Réservation introuvable.");
        r->afficher();
    }

    /** Boucle principale du programme. */
    void executer() {
        cout << "\n*** Bienvenue à l'agence " << nom_ << " - " << ville_ << " ***\n";
        bool continuer = true;
        while (continuer) {
            try {
                afficherMenu();
                const int choix = Saisie::lireEntier("Votre choix : ", 1, 10);
                cout << "\n";
                switch (choix) {
                    case 1: enregistrerPassager(); break;
                    case 2: ajouterVol(); break;
                    case 3: afficherVolsDisponibles(); break;
                    case 4: rechercherVol(); break;
                    case 5: effectuerReservation(); break;
                    case 6: enregistrerPaiement(); break;
                    case 7: menuAnnulation(); break;
                    case 8: consulterReservations(); break;
                    case 9: afficherInfosReservation(); break;
                    case 10:
                        cout << "Merci et à bientôt !\n";
                        continuer = false;
                        break;
                    default: break;
                }
            } catch (const FinDeSaisie&) {
                cout << "\nFin de la saisie. Au revoir !\n";
                continuer = false;
            } catch (const ErreurMetier& e) {
                cout << "\n[Opération refusée] " << e.what() << "\n";
            } catch (const std::exception& e) {
                cout << "\n[Erreur] " << e.what() << "\n";
            }
        }
    }

private:
    string nom_;
    string ville_;
    vector<unique_ptr<Passager>> passagers_;
    vector<unique_ptr<Vol>> vols_;
    vector<unique_ptr<Reservation>> reservations_;
    vector<unique_ptr<Paiement>> paiements_;
    int prochainIdPassager_ = 1;
    int prochainIdReservation_ = 1;
    int prochainIdPaiement_ = 1;

    /** Plus petit numéro de place non occupé par une réservation active du vol. */
    int premierePlaceLibre(const Vol& vol) const {
        vector<bool> occupee(static_cast<size_t>(vol.getCapacite()) + 1, false);
        for (const auto& r : reservations_)
            if (r->getVol() == &vol && !r->estAnnulee()) occupee[static_cast<size_t>(r->getNumeroPlace())] = true;
        for (int place = 1; place <= vol.getCapacite(); ++place)
            if (!occupee[static_cast<size_t>(place)]) return place;
        throw std::logic_error("Aucune place libre alors que le vol est disponible.");
    }

    void proposerRemboursement(Paiement& p) {
        if (p.getStatutRemboursement() != StatutRemboursement::A_REMBOURSER) return;
        if (Saisie::lireOuiNon("Rembourser le client maintenant")) {
            p.traiterRemboursement();
            cout << "Remboursement effectué : " << formaterMontant(p.getMontant()) << ".\n";
        } else {
            cout << "Le remboursement reste en attente (A_REMBOURSER).\n";
        }
    }

    void menuAnnulation() {
        cout << "--- Annulation ---\n  1. Annuler un paiement\n  2. Annuler une réservation\n  0. Retour\n";
        const int choix = Saisie::lireEntier("Votre choix : ", 0, 2);
        if (choix == 1) annulerPaiement();
        else if (choix == 2) annulerReservation();
    }
};

// ============================================================================
//  Programme principal
// ============================================================================

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);  // affichage correct des accents sous Windows
    SetConsoleCP(CP_UTF8);
#endif
    Agence agence("Cotonou Voyages", "Cotonou");
    if (argc > 1 && string(argv[1]) == "--demo") agence.chargerDonneesDemo();
    agence.executer();
    return 0;
}
