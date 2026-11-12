import java.awt.*;

public class numberPne extends Canvas implements constants {

    String theValue;
    int canvasWidth = 0;
    int canvasHeight = 0;
    Rectangle theBounds = null;
    Font theFont = new Font("Helvetica", Font.BOLD, 12);

    public numberPne(String message) {

	super();
	theValue = message;
	setBackground(highlightColor);
	setSize(100, 18);
    }

    public void paint(Graphics g) {

	if (g == null) {
	    return;
	}

	Graphics2D g2 = Theme.smooth(g);

	theBounds = getBounds();
	canvasWidth = theBounds.width - 1;
	canvasHeight = theBounds.height - 1;

	g2.setColor(highlightColor);
	g2.fillRect(0, 0, canvasWidth + 1, canvasHeight + 1);
	g2.setColor(foregroundColor);
	g2.setFont(ValueFont);
	FontMetrics fm = g2.getFontMetrics();
	g2.drawString(theValue, 2, (canvasHeight + fm.getAscent() - fm.getDescent()) / 2 + 1);
    }

    public Dimension getPreferredSize() {
	return new Dimension(90, 22);
    }

    public void changeStats(String message) {
	theValue = message;
	repaint();
    }
}
