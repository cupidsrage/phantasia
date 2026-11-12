import java.awt.*;
import java.awt.event.*;

/*
 * Theme - applies the dark Phantasia look to AWT components.
 *
 * apply() walks a window's components and sets colors and fonts.
 * install() does this automatically for every window (including all the
 * game's dialogs) as it opens, so the dialog classes need no changes.
 */
public class Theme implements constants {

    public static void apply(Component c) {

	if (c instanceof Button) {
	    c.setBackground(buttonColor);
	    c.setForeground(foregroundColor);
	    if (c.getFont() == null || c.getFont().getSize() < 12) {
		c.setFont(ButtonFont);
	    }
	}
	else if (c instanceof TextComponent || c instanceof java.awt.List ||
		c instanceof Choice) {
	    c.setBackground(fieldColor);
	    c.setForeground(foregroundColor);
	    if (c.getFont() == null || c.getFont().getSize() < 13) {
		c.setFont(UserFont);
	    }
	}
	else if (c instanceof Label) {
	    if (!c.isForegroundSet() || c.getForeground().equals(Color.black)) {
		c.setForeground(foregroundColor);
	    }
	    if (!c.isFontSet()) {
		c.setFont(LabelFont.deriveFont(15f));
	    }
	}
	else if (c instanceof Window || c instanceof Panel) {
	    c.setBackground(backgroundColor);
	    c.setForeground(foregroundColor);
	}

	if (c instanceof Container) {
	    Component[] children = ((Container) c).getComponents();
	    for (int i = 0; i < children.length; i++) {
		apply(children[i]);
	    }
	}
    }

	/* theme every window as it opens */
    public static void install() {

	Toolkit.getDefaultToolkit().addAWTEventListener(new AWTEventListener() {
	    public void eventDispatched(AWTEvent e) {
		if (e.getID() == WindowEvent.WINDOW_OPENED &&
			e.getSource() instanceof Dialog) {
		    Dialog d = (Dialog) e.getSource();
		    apply(d);
		    d.pack();
		    d.setLocationRelativeTo(d.getOwner());
		    d.repaint();
		}
	    }
	}, AWTEvent.WINDOW_EVENT_MASK);
    }

	/* smooth text and shapes in custom-painted panels */
    public static Graphics2D smooth(Graphics g) {

	Graphics2D g2 = (Graphics2D) g;
	g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING,
		RenderingHints.VALUE_ANTIALIAS_ON);
	g2.setRenderingHint(RenderingHints.KEY_TEXT_ANTIALIASING,
		RenderingHints.VALUE_TEXT_ANTIALIAS_ON);
	return g2;
    }
}
