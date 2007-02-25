#include <math.h>
#include "conducer.h"
#include "customelement.h"
#include "diagram.h"
#include "exportdialog.h"

/**
	Constructeur
	@param parent Le QObject parent du schema
*/
Diagram::Diagram(QObject *parent) : QGraphicsScene(parent) {
	setBackgroundBrush(Qt::white);
	poseur_de_conducer = new QGraphicsLineItem(0, 0);
	poseur_de_conducer -> setZValue(1000000);
	QPen t;
	t.setColor(Qt::black);
	t.setWidthF(1.5);
	t.setStyle(Qt::DashLine);
	poseur_de_conducer -> setPen(t);
	poseur_de_conducer -> setLine(QLineF(QPointF(0.0, 0.0), QPointF(0.0, 0.0)));
	draw_grid  = true;
	use_border = true;
	connect(this, SIGNAL(changed(const QList<QRectF> &)), this, SLOT(slot_checkSelectionEmptinessChange()));
}

/**
	Dessine l'arriere-plan du schema, cad la grille.
	@param p Le QPainter a utiliser pour dessiner
	@param r Le rectangle de la zone a dessiner
*/
void Diagram::drawBackground(QPainter *p, const QRectF &r) {
	p -> save();
	
	// desactive tout antialiasing, sauf pour le texte
	p -> setRenderHint(QPainter::Antialiasing, false);
	p -> setRenderHint(QPainter::TextAntialiasing, true);
	p -> setRenderHint(QPainter::SmoothPixmapTransform, false);
	
	// dessine un fond blanc
	p -> setPen(Qt::NoPen);
	p -> setBrush(Qt::white);
	p -> drawRect(r);
	
	if (draw_grid) {
		// dessine les points de la grille
		p -> setPen(Qt::black);
		p -> setBrush(Qt::NoBrush);
		qreal limite_x = r.x() + r.width();
		qreal limite_y = r.y() + r.height();
		
		int g_x = (int)ceil(r.x());
		while (g_x % GRILLE_X) ++ g_x;
		int g_y = (int)ceil(r.y());
		while (g_y % GRILLE_Y) ++ g_y;
		
		for (int gx = g_x ; gx < limite_x ; gx += GRILLE_X) {
			for (int gy = g_y ; gy < limite_y ; gy += GRILLE_Y) {
				p -> drawPoint(gx, gy);
			}
		}
	}
	
	if (use_border) border_and_inset.draw(p, MARGIN, MARGIN);
	p -> restore();
}

/**
	Exporte le schema vers une image
	@return Une QImage representant le schema
*/
QImage Diagram::toImage(int width, int height, Qt::AspectRatioMode aspectRatioMode) {
	// determine la zone source =  contenu du schema + marges
	QRectF source_area;
	if (!use_border) {
		source_area = itemsBoundingRect();
		source_area.translate(-MARGIN, -MARGIN);
		source_area.setWidth (source_area.width () + 2.0 * MARGIN);
		source_area.setHeight(source_area.height() + 2.0 * MARGIN);
	} else {
		source_area = QRectF(
			0.0,
			0.0,
			border_and_inset.borderWidth () + 2.0 * MARGIN,
			border_and_inset.borderHeight() + 2.0 * MARGIN
		);
	}
	
	// si les dimensions ne sont pas precisees, l'image est exportee a l'echelle 1:1
	QSize image_size = (width == -1 && height == -1) ? source_area.size().toSize() : QSize(width, height);
	
	// initialise une image avec ces dimensions
	QImage pix = QImage(image_size, QImage::Format_RGB32);
	
	// prepare le rendu
	QPainter p;
	if (!p.begin(&pix)) return(QImage());
	
	// rendu antialiase
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setRenderHint(QPainter::TextAntialiasing, true);
	p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	
	// deselectionne tous les elements
	QList<QGraphicsItem *> selected_elmts = selectedItems();
	foreach (QGraphicsItem *qgi, selected_elmts) qgi -> setSelected(false);
	
	// effectue le rendu lui-meme
	render(&p, pix.rect(), source_area, aspectRatioMode);
	p.end();
	
	// restaure les elements selectionnes
	foreach (QGraphicsItem *qgi, selected_elmts) qgi -> setSelected(true);
	
	return(pix);
}

/**
	Permet de connaitre les dimensions qu'aura l'image generee par la methode toImage()
	@return La taille de l'image generee par toImage()
	@todo tenir compte des arguments
*/
QSize Diagram::imageSize() const {
	// determine la zone source =  contenu du schema + marges
	qreal image_width, image_height;
	if (!use_border) {
		QRectF items_rect = itemsBoundingRect();
		image_width  = items_rect.width();
		image_height = items_rect.height();
	} else {
		image_width  = border_and_inset.borderWidth();
		image_height = border_and_inset.borderHeight();
	}
	
	image_width  += 2.0 * MARGIN;
	image_height += 2.0 * MARGIN;
	
	// renvoie la taille de la zone source
	return(QSizeF(image_width, image_height).toSize());
}

/**
	Exporte tout ou partie du schema 
	@param diagram Booleen (a vrai par defaut) indiquant si le XML genere doit
	representer tout le schema ou seulement les elements selectionnes
	@return Un Document XML (QDomDocument)
*/
QDomDocument Diagram::toXml(bool diagram) {
	// document
	QDomDocument document;
	
	// racine de l'arbre XML
	QDomElement racine = document.createElement("schema");
	
	// proprietes du schema
	if (diagram) {
		if (!border_and_inset.author().isNull())    racine.setAttribute("auteur",   border_and_inset.author());
		if (!border_and_inset.date().isNull())      racine.setAttribute("date",     border_and_inset.date().toString("yyyyMMdd"));
		if (!border_and_inset.title().isNull())     racine.setAttribute("titre",    border_and_inset.title());
		if (!border_and_inset.fileName().isNull())  racine.setAttribute("filename", border_and_inset.fileName());
		if (!border_and_inset.folio().isNull())     racine.setAttribute("folio",    border_and_inset.folio());
	}
	document.appendChild(racine);
	
	// si le schema ne contient pas d'element (et donc pas de conducteurs), on retourne de suite le document XML
	if (items().isEmpty()) return(document);
	
	// creation de deux listes : une qui contient les elements, une qui contient les conducteurs
	QList<Element *> liste_elements;
	QList<Conducer *> liste_conducers;
	
	// Determine les elements a � XMLiser �
	foreach(QGraphicsItem *qgi, items()) {
		if (Element *elmt = qgraphicsitem_cast<Element *>(qgi)) {
			if (diagram) liste_elements << elmt;
			else if (elmt -> isSelected()) liste_elements << elmt;
		} else if (Conducer *f = qgraphicsitem_cast<Conducer *>(qgi)) {
			if (diagram) liste_conducers << f;
			// lorsqu'on n'exporte pas tout le diagram, il faut retirer les conducteurs non selectionnes
			// et pour l'instant, les conducteurs non selectionnes sont les conducteurs dont un des elements n'est pas relie
			else if (f -> terminal1 -> parentItem() -> isSelected() && f -> terminal2 -> parentItem() -> isSelected()) liste_conducers << f;
		}
	}
	
	// table de correspondance entre les adresses des bornes et leurs ids
	QHash<Terminal *, int> table_adr_id;
	
	// enregistrement des elements
	if (liste_elements.isEmpty()) return(document);
	QDomElement elements = document.createElement("elements");
	foreach(Element *elmt, liste_elements) {
		elements.appendChild(elmt -> toXml(document, table_adr_id));
	}
	racine.appendChild(elements);
	
	// enregistrement des conducteurs
	if (liste_conducers.isEmpty()) return(document);
	QDomElement conducers = document.createElement("conducteurs");
	foreach(Conducer *cond, liste_conducers) {
		conducers.appendChild(cond -> toXml(document, table_adr_id));
	}
	racine.appendChild(conducers);
	
	// on retourne le document XML ainsi genere
	return(document);
}

/**
	Importe le diagram decrit dans un document XML. Si une position est
	precisee, les elements importes sont positionnes de maniere a ce que le
	coin superieur gauche du plus petit rectangle pouvant les entourant tous
	(le bounding rect) soit a cette position.
	@param document Le document XML a analyser
	@param position La position du diagram importe
	@param consider_informations Si vrai, les informations complementaires (auteur, titre, ...) seront prises en compte
	@return true si l'import a reussi, false sinon
*/
bool Diagram::fromXml(QDomDocument &document, QPointF position, bool consider_informations) {
	QDomElement racine = document.documentElement();
	// le premier element doit etre un schema
	/// @todo renommer schema en diagram
	if (racine.tagName() != "schema") return(false);
	
	// lecture des attributs de ce schema
	if (consider_informations) {
		border_and_inset.setAuthor(racine.attribute("auteur"));
		border_and_inset.setTitle(racine.attribute("titre"));
		border_and_inset.setDate(QDate::fromString(racine.attribute("date"), "yyyyMMdd"));
		border_and_inset.setFileName(racine.attribute("filename"));
		border_and_inset.setFolio(racine.attribute("folio"));
	}
	
	// si la racine n'a pas d'enfant : le chargement est fini (schema vide)
	if (racine.firstChild().isNull()) return(true);
	
	// chargement de tous les Elements du fichier XML
	QList<Element *> elements_ajoutes;
	QHash< int, Terminal *> table_adr_id;
	QHash< int, Terminal *> &ref_table_adr_id = table_adr_id;
	for (QDomNode node = racine.firstChild() ; !node.isNull() ; node = node.nextSibling()) {
		// on s'interesse a l'element XML "elements" (= groupe d'elements)
		QDomElement elmts = node.toElement();
		if(elmts.isNull() || elmts.tagName() != "elements") continue;
		// parcours des enfants de l'element XML "elements"
		for (QDomNode n = elmts.firstChild() ; !n.isNull() ; n = n.nextSibling()) {
			// on s'interesse a l'element XML "element" (elements eux-memes)
			QDomElement e = n.toElement();
			if (e.isNull() || !Element::valideXml(e)) continue;
			
			// cree un element dont le type correspond � l'id type
			QString type_id = e.attribute("type");
			QString chemin_fichier = QETApp::realPath(type_id);
			CustomElement *nvel_elmt = new CustomElement(chemin_fichier);
			if (nvel_elmt -> isNull()) {
				QString debug_message = QString("Le chargement de la description de l'element %1 a echoue avec le code d'erreur %2").arg(chemin_fichier).arg(nvel_elmt -> etat());
				delete nvel_elmt;
				qDebug(debug_message.toLatin1().data());
				continue;
			}
			
			// charge les caracteristiques de l'element
			if (nvel_elmt -> fromXml(e, ref_table_adr_id)) {
				// ajout de l'element au schema et a la liste des elements ajoutes
				addItem(nvel_elmt);
				elements_ajoutes << nvel_elmt;
			} else {
				delete nvel_elmt;
				qDebug("Le chargement des parametres d'un element a echoue");
			}
		}
	}
	
	// aucun Element n'a ete ajoute - inutile de chercher des conducteurs - le chargement est fini
	if (!elements_ajoutes.size()) return(true);
	
	// gere la translation des nouveaux elements si celle-ci est demandee
	if (position != QPointF()) {
		// determine quel est le coin superieur gauche du rectangle entourant les elements ajoutes
		qreal minimum_x = 0, minimum_y = 0;
		bool init = false;
		foreach (Element *elmt_ajoute, elements_ajoutes) {
			QPointF csg = elmt_ajoute -> mapToScene(elmt_ajoute -> boundingRect().topLeft());
			qreal px = csg.x();
			qreal py = csg.y();
			if (!init) {
				minimum_x = px;
				minimum_y = py;
				init = true;
			} else {
				if (px < minimum_x) minimum_x = px;
				if (py < minimum_y) minimum_y = py;
			}
		}
		qreal diff_x = position.x() - minimum_x;
		qreal diff_y = position.y() - minimum_y;
		foreach (Element *elmt_ajoute, elements_ajoutes) {
			elmt_ajoute -> setPos(elmt_ajoute -> pos().x() + diff_x, elmt_ajoute -> pos().y() + diff_y);
		}
	}
	
	// chargement de tous les Conducteurs du fichier XML
	for (QDomNode node = racine.firstChild() ; !node.isNull() ; node = node.nextSibling()) {
		// on s'interesse a l'element XML "conducteurs" (= groupe de conducteurs)
		QDomElement conducers = node.toElement();
		if(conducers.isNull() || conducers.tagName() != "conducteurs") continue;
		// parcours des enfants de l'element XML "conducteurs"
		for (QDomNode n = conducers.firstChild() ; !n.isNull() ; n = n.nextSibling()) {
			// on s'interesse a l'element XML "element" (elements eux-memes)
			QDomElement f = n.toElement();
			if (f.isNull() || !Conducer::valideXml(f)) continue;
			// verifie que les bornes que le conducteur relie sont connues
			int id_p1 = f.attribute("borne1").toInt();
			int id_p2 = f.attribute("borne2").toInt();
			if (table_adr_id.contains(id_p1) && table_adr_id.contains(id_p2)) {
				// pose le conducteur... si c'est possible
				Terminal *p1 = table_adr_id.value(id_p1);
				Terminal *p2 = table_adr_id.value(id_p2);
				if (p1 != p2) {
					bool peut_poser_conducer = true;
					bool cia = ((Element *)p2 -> parentItem()) -> connexionsInternesAcceptees();
					if (!cia) foreach(QGraphicsItem *item, p2 -> parentItem() -> children()) if (item == p1) peut_poser_conducer = false;
					if (peut_poser_conducer) {
						Conducer *c = new Conducer(table_adr_id.value(id_p1), table_adr_id.value(id_p2), 0, this);
						c -> fromXml(f);
					}
				}
			} else qDebug() << "Le chargement du conducer" << id_p1 << id_p2 << "a echoue";
		}
	}
	return(true);
}

/**
	Verifie si la liste des elements selectionnes a change. Si oui, le signal
	selectionChanged() est emis.
*/
void Diagram::slot_checkSelectionChange() {
	static QList<QGraphicsItem *> cache_selecteditems = QList<QGraphicsItem *>();
	QList<QGraphicsItem *> selecteditems = selectedItems();
	if (cache_selecteditems != selecteditems) emit(selectionChanged());
	cache_selecteditems = selecteditems;
}

/**
	Verifie si la selection est passe d'un etat ou elle est vide a un etat ou
	elle ne l'est pas, et inversement. Si c'est le cas, le signal
	EmptinessChanged() est emis.
*/
void Diagram::slot_checkSelectionEmptinessChange() {
	static bool selection_was_empty = true;
	bool selection_is_empty = selectedItems().isEmpty();
	if (selection_was_empty != selection_is_empty) {
		emit(selectionEmptinessChanged());
		selection_was_empty = selection_is_empty;
	}
}

/**
	@return Le rectangle (coordonnees par rapport a la scene) delimitant le bord du schema
*/
QRectF Diagram::border() const {
	return(
		QRectF(
			MARGIN,
			MARGIN,
			border_and_inset.borderWidth(),
			border_and_inset.borderHeight()
		)
	);
}
